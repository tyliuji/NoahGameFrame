// -------------------------------------------------------------------------
//    @FileName			:    NFCTileModule.cpp
//    @Author           :    LvSheng.Huang
//    @Date             :    2017-02-25
//    @Module           :    NFCTileModule
//
// -------------------------------------------------------------------------

#include "NFCTileModule.h"
#include "NFComm/NFMessageDefine/NFProtocolDefine.hpp"

bool NFCTileModule::Init()
{
	m_pNetModule = pPluginManager->FindModule<NFINetModule>();
	m_pBigMapRedisModule = pPluginManager->FindModule<NFIBigMapRedisModule>();
	m_pKernelModule = pPluginManager->FindModule<NFIKernelModule>();
	m_pLogicClassModule = pPluginManager->FindModule<NFIClassModule>();
	m_pElementModule = pPluginManager->FindModule<NFIElementModule>();
	m_pGuildRedisModule = pPluginManager->FindModule<NFIGuildRedisModule>();
	m_pGameServerNet_ServerModule = pPluginManager->FindModule<NFIGameServerNet_ServerModule>();
	m_pPlayerRedisModule = pPluginManager->FindModule<NFIPlayerRedisModule>();
	
    return true;
}

bool NFCTileModule::Shut()
{
    return true;
}

bool NFCTileModule::Execute()
{
    return true;
}

bool NFCTileModule::AfterInit()
{
	m_pKernelModule->AddClassCallBack(NFrame::Player::ThisName(), this, &NFCTileModule::OnObjectClassEvent);

	if (!m_pNetModule->AddReceiveCallBack(NFMsg::EGameMsgID::EGEC_REQ_MINING_TITLE, this, &NFCTileModule::ReqMineTile)) { return false; }

    return true;
}

void NFCTileModule::ReqMineTile(const int nSockIndex, const int nMsgID, const char * msg, const uint32_t nLen)
{
	CLIENT_MSG_PROCESS(nSockIndex, nMsgID, msg, nLen, NFMsg::ReqMiningTitle);

	int nX = xMsg.x();
	int nY = xMsg.y();
	int nOpr = xMsg.opr();
	if (1 == nOpr)//add
	{
		AddTile(nPlayerID, nX, nY);
	}
	else if (0 == nOpr)//rem
	{
		RemoveTile(nPlayerID, nX, nY);
	}
	NFMsg::AckMiningTitle xData;
	NFMsg::AckMiningTitle::TileState* pTile = xData.add_tile();
	if (pTile)
	{
		pTile->set_x(nX);
		pTile->set_y(nY);
		pTile->set_opr(nOpr);
	}


	m_pGameServerNet_ServerModule->SendMsgPBToGate(NFMsg::EGEC_ACK_MINING_TITLE, xData, nPlayerID);
}

bool NFCTileModule::AddTile(const NFGUID & self, const int nX, const int nY, const int nOpr)
{
	NF_SHARE_PTR<TileData> xTileData = mxTileData.GetElement(self);
	if (!xTileData)
	{
		xTileData = NF_SHARE_PTR<TileData>(NF_NEW TileData());
		mxTileData.AddElement(self, xTileData);
	}

	NF_SHARE_PTR<NFMapEx<int, TileState>> xStateDataMap = xTileData->mxTileState.GetElement(nX);
	if (!xStateDataMap)
	{
		xStateDataMap = NF_SHARE_PTR<NFMapEx<int, TileState>>(NF_NEW NFMapEx<int, TileState>());
		xTileData->mxTileState.AddElement(nX, xStateDataMap);
	}

	NF_SHARE_PTR<TileState> xTileState  = xStateDataMap->GetElement(nY);
	if (!xTileState)
	{
		xTileState = NF_SHARE_PTR<TileState>(NF_NEW TileState());
		xStateDataMap->AddElement(nY, xTileState);
	}
	else
	{
		if (nOpr == xTileState->state)
		{
			//has be deleted
			return false;
		}
	}
	xTileState->x = nX;
	xTileState->y = nY;
	xTileState->state = nOpr;

	return false;
}

bool NFCTileModule::RemoveTile(const NFGUID & self, const int nX, const int nY)
{
	NF_SHARE_PTR<TileData> xTileData = mxTileData.GetElement(self);
	if (!xTileData)
	{
		return false;
	}

	NF_SHARE_PTR<NFMapEx<int, TileState>> xStateDataMap = xTileData->mxTileState.GetElement(nX);
	if (!xStateDataMap)
	{
		return false;
	}

	return xStateDataMap->RemoveElement(nY);
}

bool NFCTileModule::SaveTileData(const NFGUID & self)
{
	NF_SHARE_PTR<TileData> xTileData = mxTileData.GetElement(self);
	if (!xTileData)
	{
		return false;
	}

	NFMsg::AckMiningTitle xData;
	NF_SHARE_PTR<NFMapEx<int, TileState>> xStateDataMap = xTileData->mxTileState.First();
	for (; ; xStateDataMap = xTileData->mxTileState.Next())
	{
		NF_SHARE_PTR<TileState> xStateData = xStateDataMap->First();
		for (; ; xStateData = xStateDataMap->Next())
		{
			//pb
			//xStateData
			NFMsg::AckMiningTitle::TileState* pTile = xData.add_tile();
			if (pTile)
			{
				pTile->set_x(xStateData->x);
				pTile->set_y(xStateData->y);
				pTile->set_opr(xStateData->state);
			}
		}
	}
	std::string strData;
	if (xData.SerializeToString(&strData))
	{
		return m_pPlayerRedisModule->SavePlayerTileToCache(self, strData);
	}

	return false;
}

bool NFCTileModule::LoadTileData(const NFGUID & self)
{
	std::string strData;
	if (m_pPlayerRedisModule->GetPlayerTileFromCache(self, strData))
	{
		NFMsg::AckMiningTitle xData;
		if (xData.ParseFromString(strData))
		{
			int nCount = xData.tile_size();
			for (int i = 0; i < nCount; ++i)
			{
				const NFMsg::AckMiningTitle::TileState& xTile = xData.tile(i);
				AddTile(self, xTile.x(), xTile.y(), xTile.opr());
			}

			return true;
		}
	}

	return false;
}

int NFCTileModule::OnObjectClassEvent(const NFGUID & self, const std::string & strClassName, const CLASS_OBJECT_EVENT eClassEvent, const NFDataList & var)
{
	if (CLASS_OBJECT_EVENT::COE_DESTROY == eClassEvent)
	{
		//save
		SaveTileData(self);
	}
	else if (CLASS_OBJECT_EVENT::COE_CREATE_LOADDATA == eClassEvent)
	{
		//load
		LoadTileData(self);
	}

	return 0;
}

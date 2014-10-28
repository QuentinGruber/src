#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "monitorAPI.h"

//**********************************************************************************************
//**********************************************************************************************
//**********************************  DO NOT EDIT THIS FILE  ***********************************
//**********************************************************************************************
//**********************************************************************************************
void set_bit(  unsigned char p[], int bit){  p[bit >> 3] |=   1 << (bit & 0x7);      }
void unset_bit(unsigned char p[], int bit){  p[bit >> 3] &= ~(1 << (bit & 0x7));     }
int  get_bit(  unsigned char p[], int bit){ return (p[bit >> 3] >> (bit & 0x7)) & 1; }

////////////////////////////////////////
// Player implementation
////////////////////////////////////////
MonitorObject::MonitorObject(UdpConnection *con,CMonitorData *_gamedata, char *passwd, char **addressList,bool _bprint)
{
	mbprint			= _bprint;
	mMonitorData	= _gamedata;
	mbAuthenticated	= false;
	mPasswd			= passwd;
	mAddressList	= addressList;
	mHierarchySent	= false;
	mSequence		= 1;
	mlastUpdateTime	= 0 ;
	mConnection		= con;
	mConnection->AddRef();
	mConnection->SetHandler(this);

	// Flag all the descriptions
	mMark			= new unsigned char [ (mMonitorData->DataMax()/8)+2];
	memset(mMark,0,(mMonitorData->DataMax()/8)+2);
	char hold[256];
	if( mbprint )
	{
		fprintf(stderr,"MONITOR API CONNECTED: %s:%d\n", 
			mConnection->GetDestinationIp().GetAddress(hold), 
			mConnection->GetDestinationPort());
	}
}

MonitorObject::~MonitorObject()
{
	if( mMark )
		delete [] mMark;

	if( mConnection )
	{
		mConnection->SetHandler(NULL);
		mConnection->Disconnect();
		mConnection->Release();
	}
}

void MonitorObject::OnTerminated( UdpConnection * /* con */)
{
char hold[214];

	if( mConnection )
	{
		if( mbprint )
		{
			mConnection->GetDestinationIp().GetAddress(hold);
			if( mbprint )
			{
				fprintf(stderr,"MONITOR API TERMINATE %s:%d %s - %s\n", 
					hold, 
					mConnection->GetDestinationPort(),
					UdpConnection::DisconnectReasonText( mConnection->GetDisconnectReason()),
					UdpConnection::DisconnectReasonText( mConnection->GetOtherSideDisconnectReason()));
			}
		}
		mConnection->SetHandler(NULL);
		mConnection->Disconnect();
		mConnection->Release();
		mConnection = NULL;
	}
}


void MonitorObject::OnRoutePacket(UdpConnection * con,  const uchar *data, int dataLen)
{

	if( dataLen < 6 )
	{
		if( mbprint )
			fprintf(stderr,"MONITOR API: This should not happen, line %d\n",__LINE__);
	}

	simpleMessage msg(data);

	if( con == NULL )
	{
		if( mbprint)
			fprintf(stderr,"MONITOR API: MonitorObject.OnRoutePacket, recived a NULL connection.?\n");
		return;
	}

	//*************************  Login Process ***************************
	if ( mbAuthenticated == false  )
	{ 
		if ( msg.getCommand() != MON_MSG_AUTH )
				return;

		if( processAuthRequest(data, dataLen) == false )
			return;

		mbAuthenticated = true;
		return;
	}

	//*************************  Process Commands  ***********************
	switch(msg.getCommand())
	{

	// ***********************  Game Server Processing  ******************
	case MON_MSG_QUERY_ELEMENTS:
		if( mHierarchySent == true )
		{ 
			if( mMonitorData->processElementsRequest(con,mSequence,(char *)&data[6],dataLen,mlastUpdateTime ))
				mlastUpdateTime = time(NULL);
			break;
		}
		// NOTE: if mHierarchy is not sent or changed, then send it.

	case MON_MSG_QUERY_HIERARCHY:
		if( mMonitorData->processHierarchyRequest(con,mSequence) == false)
		{
			break;
		}
		mHierarchySent = true;
		break;

	case MON_MSG_QUERY_DESCRIPTION:
		mMonitorData->processDescriptionRequest(con,mSequence,(char *)&data[6], dataLen,mMark);
		break;

	case MON_MSG_ERROR:
		processError(data);
		break;

	default:
		fprintf(stderr,"MONITOR API: unknown message type received from client [%x]\n",msg.getCommand());
		break;
	}
}


bool MonitorObject::checkAddress(const char * address)
{
int x = 0;
	while( mAddressList[x] )
	{
		if (strncmp(mAddressList[x], address, strlen(mAddressList[x]))== 0) 
			return true;
		x++;
	}
	return false;
}

bool MonitorObject::checkPasswd( const char *passwd)
{
	if (strcmp(mPasswd,passwd)== 0 )
		return true;
	return false;	
}

bool MonitorObject::processAuthRequest(const unsigned char * data, int /* dataLen */)
{
char reply;
stringMessage strMsg(data);
char addBuff[16] = {0};
char sendBuf[32];
int len;


	reply = '1';
	
	if ( checkAddress(mConnection->GetDestinationIp().GetAddress(addBuff)) == false)
	{
		reply = '3';
	}
	else if (checkPasswd(strMsg.getData()) == false)
	{
		reply = '2';
	}

	memset(sendBuf, 0, sizeof(sendBuf));
	
	len = 0;
	packShort( sendBuf + len, len,(short)MON_MSG_AUTHREPLY);
	packShort( sendBuf + len, len, mSequence);
	packShort( sendBuf + len, len,(short)3);
	packShort( sendBuf + len, len,(short)CURRENT_API_VERSION);
	packByte(  sendBuf + len, len, reply);
	
	mConnection->Send(cUdpChannelReliable1, sendBuf, 9 );
	return true;
}

void MonitorObject::DescriptionMark(int x,int mode)
{
	if( mode == 0 ) set_bit(mMark,x); else unset_bit(mMark,x);
}

char * getErrorString(unsigned short errorCode)
{
	if (errorCode == 0)
	{
		return "none";
	}
	else
	{
		switch (errorCode)
		{
		case INVALID_HEADER:
		case INVALID_SEQUENCE:
		case INVALID_MSG_LENGTH:
		case INVALID_MSG_TYPE:
			return "Invalid message";

		case INVALID_AUTH_REQUEST:
			return "Invalid authorization request";

		case INVALID_ELEMENT_REQUEST:
		case INVALID_HIERARCHY_REQUEST:
			return "Invalid data request";

		case INVALID_AUTH_REPLY:
			return "Invalid reply to authentication request";

		case INVALID_ELEMENT_REPLY:
		case INVALID_HIERARCHY_REPLY:
			return "Badly formatted data in dataReplyMessage";

		case INVALID_ERROR_CODE:
			return "Received invalid error message";

		}
	}
	return "Undefined error code";
}

bool MonitorObject::processError(const unsigned char * data)
{
	stringMessage strMsg(data);
	unsigned short errCode = (unsigned short)atoi(strMsg.getData());
	fprintf(stderr,"MONITOR API Error: %s\n", getErrorString(errCode));
	return true;
}

///////////////////////////////////////
// MonitorManager implementation
////////////////////////////////////////
MonitorManager::MonitorManager(char *configFile, CMonitorData *_gamedata, UdpManager *manager, bool _bprint)
{
	mManager = manager;
	mbprint = _bprint;
	passString   = NULL;
	mMonitorData = _gamedata;
	mObjectCount = 0;

	for(int x=0;x<AUTHADDRESS_MAX;x++)
		allowedAddressList[x] = 0;
	
	loadAuthData(configFile);
}

MonitorManager::~MonitorManager()
{
int x;

	x=0;
	while( allowedAddressList[x])
		free(allowedAddressList[x++]);

	if( passString )
		free( passString );

	for (int i =0; i < mObjectCount; i++ )
		delete mObject[i];
}

void MonitorManager::OnConnectRequest(UdpConnection *con)
{
	if( mObjectCount == CONNECTION_MAX )
	{
		if( con )
		{
			con->SetHandler(NULL);
			con->Disconnect();
			con->Release();
		}
		if( mbprint )
			fprintf(stderr,"MonitorAPI: Collector connection reached max (%d).\n",CONNECTION_MAX);
		return;
	}
	
	AddObject(new MonitorObject(con,mMonitorData,passString,allowedAddressList,mbprint));
}

void MonitorManager::AddObject(MonitorObject  *Object)
{
	mObject[mObjectCount++] = Object;
}

void MonitorManager::GiveTime()
{
	// check if the monitor object is no longer connected
	for (int i = 0; i < mObjectCount; i++)
	{
		if( mObject[i]->mConnection  == NULL || 
			mObject[i]->mConnection->GetStatus() == UdpConnection::cStatusDisconnected )
		{
			MonitorObject *o = mObject[i];
			mObjectCount--;
			memmove(&mObject[i], &mObject[i + 1], (mObjectCount - i) * sizeof(MonitorObject *));
			i--;
			delete o;
		}
	}
}

void MonitorManager::HierarchyChanged()
{
	for (int i = 0; i < mObjectCount; i++)
		mObject[i]->HeirarchyChanged();
}

void MonitorManager::DescriptionMark(int x ,int mode)
{
	for (int i = 0; i < mObjectCount; i++)
		mObject[i]->DescriptionMark(x,mode);
}

bool MonitorManager::loadAuthData(const char * filename)
{
int nline;
int len;
int x;
char buffer[1024];

	FILE *fp = fopen(filename,"r");
	if (fp == NULL)
	{
		fprintf(stderr,"Monitor API: could not open %s file\nTHIS FILE IS REQUIRED.\n", filename);
		return false;
	}
	
	if( passString )
		free(passString);


	for(x=0;x<AUTHADDRESS_MAX;x++)
	{
		if( allowedAddressList[x] )
		{
			free(allowedAddressList[x]);
			allowedAddressList[x] = 0;
		}
	}

	nline = 0;
	x = 0;
	while(!feof(fp))
	{
		fgets( buffer, 1023, fp);

		// get rid of '\n' and '\r' for comparisons
		strtok(buffer,"\r\n");
		len = strlen(buffer);
		if( len > 0 )
		{
			if( nline == 0 )
			{
				passString = (char *)malloc( len + 1 );
				strcpy(passString,buffer);
			}
			else
			{
				allowedAddressList[x] = (char *)malloc( len + 1 );
				strcpy(allowedAddressList[x],buffer);
				x++;
			}
		}
		nline++;
	} 

	// clean up
	fclose(fp);
	return true;
}

//******************************************************************************************************
//******************************************************************************************************
//******************************************************************************************************
//******************************************************************************************************

CMonitorAPI::CMonitorAPI( char *configFile, unsigned short Port, bool _bprint , char *address, UdpManager * mang ) 
{
	mbprint = _bprint;
	mPort = Port;

	mAddress = NULL;
	if( address )
	{
		mAddress = (char *)malloc(strlen(address)+1);
		strcpy(mAddress,address);
	}

	if( mang == NULL )
	{
		UdpManager::Params params;
		params.handler			= NULL;
		params.maxConnections	= CONNECTION_MAX;
		params.outgoingBufferSize = 1000000;
		params.noDataTimeout	=            130000;
		params.oldestUnacknowledgedTimeout = 120000;
		params.processIcmpErrors = false;
		params.port				= mPort;
		mManager				= new UdpManager(&params);
		mMonitorData			= new CMonitorData();
	}
	
	mObjectManager = new MonitorManager(configFile,mMonitorData,mManager,mbprint);

	mManager->SetHandler(mObjectManager);

	if( mbprint )
		fprintf(stderr,"MonitorAPI: started on port->%d\n",mPort);
}
	
CMonitorAPI::~CMonitorAPI()
{
	if( mAddress )
		free(mAddress);

	if( mObjectManager )
		delete mObjectManager;

	mManager->Release();

	if( mMonitorData )
		delete mMonitorData;

}

void CMonitorAPI::Update()
{
	if( mManager )
		mManager->GiveTime();
	if( mObjectManager )
		mObjectManager->GiveTime();
}

void CMonitorAPI::add(const char *label, int id, int ping, char *des )
{		
	if( mMonitorData->add(label,id,ping,des))	
		mObjectManager->HierarchyChanged();
}

void CMonitorAPI::remove(int Id)
{		
	mMonitorData->remove(Id);	
	mObjectManager->HierarchyChanged();
}

void CMonitorAPI::setDescription( int Id, const char *Description ) 
{
int x;
int mode;
	
	x = mMonitorData->setDescription(Id,Description,mode);	
	if( x == -1 )
		return;
	mObjectManager->DescriptionMark(x,mode);
}

void CMonitorAPI::dump(){ mMonitorData->dump(); }

//----------------------------------------------------------------
monMessage::monMessage():command(0),sequence(0),size(0){}

//----------------------------------------------------------------
monMessage::monMessage(short cmd,short seq,short s):command(cmd),sequence(seq),size(s){}

//----------------------------------------------------------------
monMessage::monMessage(const unsigned char * source):command(0),sequence(0),size(0)
{	
int len;

	len = 0;
	unpackShort( (char *)source + len, len,command);
	unpackShort( (char *)source + len, len,sequence);
	unpackShort( (char *)source + len, len,size);
}

//----------------------------------------------------------------
monMessage::monMessage(const monMessage &copy):command(copy.command),sequence(copy.sequence),size(copy.size){}

//----------------------------------------------------------------
monMessage::~monMessage(){}


//----------------------------------------------------------------
stringMessage::stringMessage(const unsigned char *  source):monMessage(source)
{
int size;

	size = strlen((char *)source+6) +1;
	data = new char [ size ];
	strcpy(data,(char *)source+6);			// Add six for the size of the header
}

//----------------------------------------------------------------
stringMessage::stringMessage(const unsigned short command,const unsigned short sequence,const unsigned short size,char * newData):
monMessage(command, sequence, size)
{
	data = new char [strlen(newData) + 1];
	strncpy(data, newData, strlen(newData + 1));
}

//----------------------------------------------------------------
stringMessage::~stringMessage()
{
	if(data)
		delete [] data;
	data = 0;
}

//----------------------------------------------------------------
authReplyMessage::authReplyMessage(const unsigned char *  source) :
monMessage(source)
{
	int len = 6;
	unpackShort((char *)source+len,len,version);
	unpackByte((char *)source+len,len,data);
}

//----------------------------------------------------------------
authReplyMessage::authReplyMessage(const unsigned short command, 
						 const unsigned short sequence, 
						 const unsigned short size, 
						 unsigned char newData):
monMessage(command, sequence, size),data(newData){}

//----------------------------------------------------------------
authReplyMessage::~authReplyMessage(){}

//----------------------------------------------------------------
dataReplyMessage::dataReplyMessage(const unsigned char * source) :
monMessage(source)
{
	data = new unsigned char[getSize()+1];
	
	memcpy(data, source + 6, getSize());
	data[getSize()] = 0;
}

//----------------------------------------------------------------
dataReplyMessage::dataReplyMessage(const unsigned short command, 
						 const unsigned short sequence, 
						 const unsigned short size, 
						 unsigned char * newData,
						 int newDataLen):
monMessage(command, sequence, size)
{
	data = new unsigned char[newDataLen];
	memcpy(data, newData, newDataLen);
}

//----------------------------------------------------------------
dataReplyMessage::~dataReplyMessage()
{
	delete [] data;
	data = 0;
}

//----------------------------------------------------------------
simpleMessage::simpleMessage(const unsigned char * source):monMessage(source){}
//----------------------------------------------------------------
simpleMessage::simpleMessage(const unsigned short command, 
						 const unsigned short sequence, 
						 const unsigned short size):
monMessage(command, sequence, size){}
//----------------------------------------------------------------
simpleMessage::~simpleMessage(){}

//----------------------------------------------------------------



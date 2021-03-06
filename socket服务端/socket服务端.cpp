// socket服务端.cpp : 定义控制台应用程序的入口点。
///////////////////////////////////////////////
#pragma once
#include "stdafx.h"
#include <iostream>
#include <process.h>
#include <winsock2.h>
#include <ObjBase.h>
//#include <pthread.h>
#include <stdio.h>
#include <time.h>
#include <string>// 注意是C++标准库是<string>，不是<string.h>，带.h的是C语言中的头文件
#include <fstream>
#include <WinBase.h>
#include "SocketTCP.h"
#include "SocketAPP.h"
#include "mcom.h"
#include "CSmtp.h"//发邮件类

//#include <Windows.h>////<Windows.h>这个头文件不能在<winsock2.h>前面
///////////////////////////////////////////

#include "MyThreadPool.h"//注意，这头文件里头有windows.h,//<Windows.h>这个头文件不能在<winsock2.h>前面
#include "MyThread.h"  ////<Windows.h>这个头文件不能在<winsock2.h>前面
#include "TestTask.h"
///////////////////////////////////////////
#ifdef  WIN_64_DEF
#include <mysql.h>
#pragma comment(lib,"libmysql.lib") 
#endif

#pragma comment (lib,"ole32.lib") 
#pragma comment (lib, "ws2_32.lib")  //加载 ws2_32.dll
///////////////////////////////////////////////////
class  CMySaveQueue;

using namespace std;
////////////////////////////////////////////////////
//IOCPContextKey* pContextKey = new IOCPContextKey; //创建一块结构体内存,给IOCP完成端口存数据用

///////////////////////////////////////////////////
#define TEST_BUFFER_SIZE       1000
///////////////////////////////////////////////////
HANDLE  RecEvent[2];  //两事件 
HANDLE  WX_CheckAlarmEvent[2];//微信查询报警表的事件
HANDLE  WX_GetTokenEvent[2];//微信获取ACESS_TOKEN事件
//////////////////////////////////////////////////////
char buffer[TEST_BUFFER_SIZE]={
"shaozhanyu-socketconncetOK!!"
};
char m_uuid[GUID_LEN] = { 0 }; 
//////////////////////////////////////////////////////
string  RecStr="";//定义cstring 类对象
SOCKET servSock;
#ifdef  WIN_64_DEF
MYSQL mysql;
MYSQL_RES* result;
MYSQL_ROW row; 
#endif
///////////////////////////////////////////////////////
CSocketTCP   IOCPsocket;//创建自定义类，在他的构造函数中，调用了win2sock加载配置
CSocketAPP   APPsocket;
unsigned  long  totalcnt=0;//总计数
SocketRecUnit   m_SocReUnit; //硬件监听数据缓存结构体，包括socket连接
SocketRecUnit   mAPP_RevUnit; //APP数据缓存结构体，包括socket连接
string   card_list;//card列表
/////////////////////////////////////////////	
int len=0;  
int  reccnt=0;
//////////////////////////////////////////////////////////////////////////////////////////////////////////
int   socketnum[100]={0};
int   sockecnt=0;//socket客户端连接数
RFcard  RfCardData[RF_CARD_NUM];

CMyThreadPool  WorkDatathreadpool; //硬件交互数据解包线程池
CMyThreadPool  SaveDatathreadpool;//硬件上传数据存储线程池
CMyThreadPool  APPthreadpool; //APP交互数据线程池

CRITICAL_SECTION  m_RECdataLock;//硬件数据接临界区声明
CRITICAL_SECTION  mAPP_RECLock;//APP数据接收临界区声明
CRITICAL_SECTION  mSaveDataLock;//硬件上传的数据存储线程临界区声明
CRITICAL_SECTION  card_list_Lock;//硬件上传的数据存储线程临界区声明
////////////////////////////////////////////////////////////////////////////////////////////////////////

///////////////////////硬件socket服务端监听等待链接accept///////////////////////////////////////////////////////////////
int   socket_accept_thread(void *ptr)
{
	SOCKADDR_IN     connetAdrr;
	int len = sizeof(sockaddr);

	SOCKET  max_value = 0xfffffffffffffffe;

	while (true) { //死循环等待连接accept

		printf("服务器listen监听的硬件socket号: %d", IOCPsocket.m_sockfd);
		IOCPsocket.m_newClinetSockfd = accept(IOCPsocket.m_sockfd, (struct sockaddr*)&connetAdrr, &len);//等待socket客户端连接

		if (IOCPsocket.m_newClinetSockfd == INVALID_SOCKET)
		{
			Sleep(500);
			printf("硬件客户端连接无效accept返回值%d\r\n", IOCPsocket.m_newClinetSockfd);
			IOCPsocket.closeAllClient(); //关闭完成端口，释放相关资源，清除连接
			IOCPsocket.Create("", MYPORT, true);//创建SOCKET服务端，针对硬件连接
			IOCPsocket.Listen(10);
			continue;
		}
		else  if (IOCPsocket.m_newClinetSockfd < max_value)
		{
			if (IOCPsocket.m_setIOCPKEY.size() < MAX_HARD_TCP_NUM) //判断连接数
			{
				printf("\n接入一个硬件客户端：%s,累计连接数%d\r\n", inet_ntoa(connetAdrr.sin_addr), IOCPsocket.m_setIOCPKEY.size());
			}
			else
			{
				printf("硬件连接数已满\n");
				shutdown(IOCPsocket.m_newClinetSockfd, SD_BOTH);
				closesocket(IOCPsocket.m_newClinetSockfd);
				continue;
			}
		}

		try {

			IOCPContextKey * pContextKey = new IOCPContextKey; //创建一块结构体内存,给IOCP完成端口存数据用													
			CMutex::Lock lock(IOCPsocket.m_mutex);//上锁，进入临界区,构造函数中调用的

			memset(pContextKey->szMessage, 0, pContextKey->Buffer.len);//当前缓存清0
			memset(pContextKey->Buffer.buf, 0, pContextKey->Buffer.len);//当前缓存清0
			pContextKey->clientSocket = IOCPsocket.m_newClinetSockfd;//保存当前连接号
			pContextKey->opType = RECV_POSTED;
			IOCPsocket.m_setIOCPKEY.insert(pContextKey);//创建添加一个客户端key	
			//绑定IOCP完成端口，注意参数类型(ULONG_PTR)pContextKey，如果强制转换DWORD小了，64位系统会野指针
			CreateIoCompletionPort((HANDLE)IOCPsocket.m_newClinetSockfd, IOCPsocket.m_CompletionPort, (ULONG_PTR)pContextKey, 0);//当前连接绑定到IOCP完成端口
																																 //投递第一个rev信息
			DWORD Flags = 0;
			int iRev = WSARecv(IOCPsocket.m_newClinetSockfd, &pContextKey->Buffer, 1, &pContextKey->NumberOfBytesRecv, &Flags,
				&IOCPsocket.m_overlap, NULL);
			//printf("绑定IOCP端口成功，已经投递接收事件\r\n");
			//int iRetSend = WSASend(IOCPsocket.m_newClinetSockfd , (LPWSABUF)buffer ,1,(LPDWORD)strlen(buffer) ,0, &IOCPsocket.m_overlap,NULL); //发送剩余的   

		}
		catch (const bad_alloc * E) {
			cout << "pContextKey申请内存空间失败" << E << endl;	
			//system("pause");
			//return -1;
		}
	}
	return 0;

}

////////////////////////////服务端等待连接///////////////////////////////////////////////////////////////////////
int   APP_accept_thread(void *ptr)
{
	SOCKADDR_IN    connetAdrr;
	int len = sizeof(sockaddr);
	SOCKET  max_value = 0xfffffffffffffffe;
	while (true) { //等待连接accept

		printf("服务器listen监听的APP软件socket号: %d", APPsocket.m_sockfd);
		APPsocket.m_newClinetSockfd = accept(APPsocket.m_sockfd, (struct sockaddr*)&connetAdrr, &len); //等待socket客户端连接
		if (APPsocket.m_newClinetSockfd == INVALID_SOCKET)
		{
			Sleep(500);
			printf("APP客户端连接无效accept返回值%d\n", APPsocket.m_newClinetSockfd);
			APPsocket.closeAllClient(); //关闭完成端口，释放相关资源
			APPsocket.Create("", APP_PORT, true); //创建socket服务，针对APP连接
			APPsocket.Listen(10);
			continue;
		}
		else  if (APPsocket.m_newClinetSockfd < max_value)
		{
			if (APPsocket.m_setIOCPKEY.size() < MAX_APP_TCP_NUM) //判断连接数
			{
				printf("\n接入一个APP客户端：%s, 累计连接数%d\r\n", inet_ntoa(connetAdrr.sin_addr), APPsocket.m_setIOCPKEY.size());
			}
			else
			{
				printf("APP连接数已满\n");
				shutdown(APPsocket.m_newClinetSockfd, SD_BOTH);
				closesocket(APPsocket.m_newClinetSockfd);
				continue;
			}


		}
		try {
			
			IOCPContextKeyAPP* pContextKey = new IOCPContextKeyAPP;//新建用户数据缓存区

			CLockMutex::Lock lock(APPsocket.m_mutex);//上锁，进入临界区,构造函数中调用的
			
			memset(pContextKey->szMessage, 0, pContextKey->Buffer.len);//当前缓存清0
			pContextKey->clientSocket = APPsocket.m_newClinetSockfd;//保存当前socket连接号
			pContextKey->opType = APP_RECV_POSTED;
			APPsocket.m_setIOCPKEY.insert(pContextKey);//添加一个客户端key到集合中
			//绑定IOCP完成端口，注意参数类型(ULONG_PTR)pContextKey，如果强制转换DWORD小了，64位系统会野指针
			CreateIoCompletionPort((HANDLE)APPsocket.m_newClinetSockfd, APPsocket.m_CompletionPort, (ULONG_PTR)pContextKey, 0);//当前socket绑定到IOCP完成端口

																															   //投递第一个rev信息;
			DWORD Flags = 0;
			int iRev = WSARecv(APPsocket.m_newClinetSockfd, &pContextKey->Buffer, 1, &pContextKey->NumberOfBytesRecv, &Flags,
				&APPsocket.m_overlap, NULL);
		}
		catch (const bad_alloc * E) {
			cout << "pContextKey申请内存空间失败" << E << endl;
			//system("pause");
			//return -1;
		}
	}

	return 0;

}

////////////////////////定时保存数据线程////////////////////////////////////
DWORD  WINAPI  SaveFileDataThread (LPVOID lpParameter)
{  
	FILE *fpin;
	time_t   rawtime;
	struct tm *curTime;
	char filename[256];

	while(true)  
	{  
		//每次等5000毫秒   
		int nIndex = WaitForMultipleObjects(1, RecEvent, FALSE,2000);     

		if (nIndex == WAIT_OBJECT_0 + 1)   
		{  
			//第二个事件发生   //ExitThread(0);   //break;  
			/*********/
			time(&rawtime);//获取当前时间
			curTime = localtime ( &rawtime );
			//strcat(filename,inet_ntoa(connetAdrr.sin_addr));
			sprintf(filename,"%04d-%02d-%02d-%02d-%02d-%02d.txt",curTime->tm_year+1900,
			curTime->tm_mon+1,curTime->tm_mday,curTime->tm_hour,curTime->tm_min,
			curTime->tm_sec);
			
			//printf("%s",filename);
			fpin=fopen(filename,"wb");//以写的方式打开文件
			if(!fpin)//判断文件打开是否成功
			{
				printf("file creat wrong\n");
				exit(1);
			}
			fclose(fpin);//关闭文件
			memset(filename , 0 ,sizeof(filename));
			/**********/
		}   
		else if (nIndex == WAIT_OBJECT_0) //第一个事件发生    
		{   
			//第一个事件  
			if(m_SocReUnit.DataLen)
			time(&rawtime);//获取当前时间
			curTime = localtime ( &rawtime );
			sprintf(filename,"%04d-%02d-%02d-%02d-%02d-%02d.jpg",curTime->tm_year+1900,
			curTime->tm_mon+1,curTime->tm_mday,curTime->tm_hour,curTime->tm_min,
			curTime->tm_sec);
			//printf("%s",filename);
			fpin=fopen(filename,"wb");//以写的方式打开文件
			if(!fpin)//判断文件打开是否成功
			{
				printf("file creat wrong\n");
				exit(1);
			}
			fseek(fpin, 0, SEEK_END);//指向文件末尾
			fwrite (m_SocReUnit.RecData , sizeof(unsigned char), m_SocReUnit.DataLen , fpin);
			//printf("文件写入完成!\n");//写入完成提示
			fclose(fpin);//关闭文件
			//printf("jpg文件保存成功!\n");//

		}    
		else if (nIndex == WAIT_TIMEOUT) //超时500毫秒    
		{   //超时可作定时用    
	
		}   
	}  
	 printf("线程结束\n");  

	 return 0;
	
}


// 发送信息的线程执行函数
DWORD WINAPI ServerSendThread(LPVOID IpParam)
{
	printf("socket发送线程！\r\n");
	
	//WaitForSingleObject();
	return 0;
}

#ifdef  WIN_64_DEF
//////////////////////初始化配置数据库用户名/////////////////////////////////
int  InitInsertBikeDatabaseUser()
{
	int i,j;
	const char user[] = "root";         
    const char pswd[] = "123456";        
    const char host[] = "localhost";    
    char table[] = "bike";    
	char  card[]="01000000";
	unsigned  char  cardhex[4]={0x01,0x00,0x00,0x00};//卡号的字节显示4个字节
	DWORD  card4bytes = 0x01000000;
	char  insertUserSQL[]= "INSERT INTO user(username, userpwd,card,bikename,token,time)    \
			 VALUES(NULL,NULL,'12000000',NULL,'12345', NULL )";
    char *pch;
	unsigned int port = 3306;        
        
    MYSQL myCont;
//    MYSQL_RES *result;
//    MYSQL_ROW sql_row;
    int res;
   if (mysql_init(&myCont) == NULL)//初始化mysql
   {
	   return -11;
   }
    if (mysql_real_connect(&myCont, host, user, pswd, table, port, NULL, 0))
    {
        //mysql_query(&myCont, "SET NAMES utf8"); //设置编码格式
      
    }
    else
    {
        cout << "connect failed!" << endl;
		mysql_close(&myCont);
		return 0;
    }

	pch=strstr(insertUserSQL,"12000000");
	if(pch!=NULL)
	{
		printf("\nfindSUCESS!\n");
	}
	else
	{	
		mysql_close(&myCont);
		return 0;
	}
	card4bytes =0;
	for(i=0;i<1000;i++)
	{
		cardhex[3] = card4bytes&0x000000ff;
		cardhex[2] = (card4bytes>>8)&0x000000ff;
		cardhex[1] = (card4bytes>>16)&0x000000ff;
		cardhex[0] = (card4bytes>>24)&0x000000ff;
		
		for(j=0;j<4;j++)
		sprintf(&card[2*j], "%02x", cardhex[j]); //小写16 进制，宽度占2个位置，右对齐，不足补0

		memcpy(pch,card,8);
		res = mysql_query(&myCont, insertUserSQL);
        
		if (!res) 
		{
			printf("Inserted %lu rows\n", (unsigned long)mysql_affected_rows(&myCont));
		} 
		else 
		{
			fprintf(stderr, "Insert error %d: %s\n", mysql_errno(&myCont),
			mysql_error(&myCont));
		}
		card4bytes++;
	}
	mysql_close(&myCont);
	return 1;
}
/////////////////////初始化配置数据库卡号/////////////////////////////////////////////
int  InitInsertBikeDatabaseCardinfo()
{
	int i,j;
	const char user[] = "root";         
    const char pswd[] = "123456";        
    const char host[] = "localhost";    
    char table[] = "bike";    
	char  card[]="01000000";
	unsigned int port = 3306; 

	unsigned  char  cardhex[4]={0x01,0x00,0x00,0x00};//卡号的字节显示4个字节
	DWORD  card4bytes = 0x01000000;
	string str_card , str_uuid;
	
    //char *pch;	       
        
    MYSQL myCont;
//    MYSQL_RES *result;
//    MYSQL_ROW sql_row;
    int res;


   if (mysql_init(&myCont) == NULL)//初始化mysql
   {
	   return -11;
   }
    if (mysql_real_connect(&myCont, host, user, pswd, table, port, NULL, 0))
    {
        //mysql_query(&myCont, "SET NAMES utf8"); //设置编码格式
       
    }
    else
    {
        cout << "connect failed!" << endl;
		mysql_close(&myCont);
		return 0;
    }

	///////////////////////////////////////////////////////////////////////

	card4bytes =0;
	for(i=0;i<10000;i++)
	{
		cardhex[3] = card4bytes&0x000000ff;
		cardhex[2] = (card4bytes>>8)&0x000000ff;
		cardhex[1] = (card4bytes>>16)&0x000000ff;
		cardhex[0] = (card4bytes>>24)&0x000000ff;
		
		for(j=0;j<4;j++)
		sprintf(&card[2*j], "%02x", cardhex[j]); //小写16 进制，宽度占2个位置，右对齐，不足补0
		str_card = card;
		str_uuid = MakeUUID();
		string  InsertCard  = "INSERT INTO cardinfo( card , used ,serialnumber ,device ,time)    \
			 VALUES(  '" + str_card + "',NULL ,'" + str_uuid + "', NULL , NOW() )";

		res = mysql_query(&myCont,   (const  char *)InsertCard.c_str());
        
		if (!res) 
		{
			printf("Inserted %lu rows\n", (unsigned long)mysql_affected_rows(&myCont));
		} 
		else 
		{
			fprintf(stderr, "Insert error %d: %s\n", mysql_errno(&myCont),
			mysql_error(&myCont));
		}
		card4bytes++;
	}
	
    mysql_close(&myCont);
	return 1;
}


/////////////////////更新卡状态/////////////////////////////////////////////
int  UpdateCardState()
{
//	int i;
	const char user[] = "root";         
    const char pswd[] = "123456";        
    const char host[] = "localhost";    
    char table[] = "bike";    
	char bike_username[32]="";
	char bike_userpwd[32]="";
	char bike_token[66]="";
	int  bike_app_socket=0;
	int  bike_setlock=0;
	int  bike_update_card =0;
	char bike_card[32]="";
	int  bike_card_socket =1;
	char bike_card_state='I';
	int  bike_card_lock=0;
	char bike_bike_name[64]="";
	char bike_gps[50]=",A,3158.4608,N,11848.3737,E,10.05";
	//struct tm   bike_time;  
/***
	char  UpdateCardStateSQL[]= "UPDATE user SET username='shaozhanyu', userpwd='123456',token='abcd1234567890',app_socket=454,\
				setlock=1,update_card=1,card_socket=251,card_state='IN',card_lock=0,gps=',A,3158.4608,N,11848.3737,E,10.05'  \
				WHERE bikename='abcd'";
***/
	char  UpdateCardStateSQL[]= "UPDATE user SET  userpwd='123456',token='abcd1234567890',app_socket=454,\
				setlock=1,update_card=1,card_socket=251,card_state='IN',card_lock=0,gps=',A,3158.4608,N,11848.3737,E,10.05',  \
				time='2017-5-10-10:02:05', bikename='lvju'   WHERE  username='shaozhanyu'";

//    char *pch;
	unsigned int port = 3306;        
        
    MYSQL myCont;
   // MYSQL_RES *result;
//    MYSQL_ROW sql_row;
    int res;


   if (mysql_init(&myCont) == NULL)//初始化mysql
   {
	   return -11;
   }
    if (mysql_real_connect(&myCont, host, user, pswd, table, port, NULL, 0))
    {
        //mysql_query(&myCont, "SET NAMES utf8"); //设置编码格式
        printf("connect SQL sucess!\n");
    }
    else
    {
        cout << "connect failed!" << endl;
		mysql_close(&myCont);
		return 0;
    }

	///////////////////////////////////////////////////////////////////////
	
	res = mysql_query(&myCont, UpdateCardStateSQL); //执行SQL语句
	printf("update SQL sucess!\n");
    mysql_close(&myCont);

	return 1;

}

#endif

////////////////////生成UUID//////////////////////////////////////
char*  MakeUUID( )
{
	 
	char *uuid = m_uuid;
    GUID guid; //对象 

    if (CoCreateGuid(&guid))  
    {  
        fprintf(stderr, "create guid error\n");  
        return NULL;
    }  

    _snprintf(m_uuid, sizeof(m_uuid),  
        "%08X-%04X-%04x-%02X%02X-%02X%02X%02X%02X%02X%02X",  
        guid.Data1, guid.Data2, guid.Data3,  
        guid.Data4[0], guid.Data4[1], guid.Data4[2],  
        guid.Data4[3], guid.Data4[4], guid.Data4[5],  
        guid.Data4[6], guid.Data4[7]);  

    printf("guid: %s\n", m_uuid);  
	return uuid;
}
//////////////////////////////////////////////////////////////////
int _tmain(int argc, _TCHAR* argv[])
{
	FILE *fpin;
	time_t   rawtime;
	struct tm *curTime;
	char filename[256];
    int i=0,j=0,mymax=-99999;
	//char a;
	char inputcmd[50];
	int ret=0;	
	int  feizuse=1;
	int  thread1=15;
	int  thread2=18;
	int  thread3=21;
	int  thread4=23;
	int  thread5=27;
	int *pst=&thread1;
	int *pst2=&thread2;
	int *pst3 = &thread3;
	int *pst4 = &thread4;
	int *pst5 = &thread5;
	sockecnt=0;//socket客户端连接数

	//InitInsertBikeDatabaseCardinfo();//初始化配置card
#if  0
//测试硬件上传GPS数据
//数据格式： 头+卡号+GPS数据 
unsigned  char     GpsData[120]={
0x96,0x69,0x96,0x69,
0x5C,
0x01,0x00,0x01,0x01,
0x01,0x02,0x03,0x04,
0x01,
0x00,0x08,
0x00,0x05,
0x30,0x30,0x30,0x30,0x30,
0x01,0x06,0x00,0x00,0x00,0x04,0x49,0x01,
0xff
};

char   gpstr[40]="A,3158.4608,N,11848.3737,E,10.05";//GPS维度，经度，速度
memcpy(GpsData +31, gpstr ,strlen(gpstr));
GpsData[15]=strlen(gpstr)+8;
SaveGPSData(1234 , (char*)(GpsData+23) , GpsData[14]*256+GpsData[15] );
#endif		

	//InitInsertBikeDatabaseUser();
	//InitInsertBikeDatabaseCardinfo();
	//UpdateCardState();//修改一行的某个字段值

	char  json_str[]={"{\
	\"errno\": 0,\
	\"error\":\"succ\",\
	\"data\":\
	{\
		\"card\":\"00000002\",\
		\"card_state\":\"In\",\
		\"card_lock\":1,\
		\"token\":\"EFEW-EFW-EFW-WEFE\",\
		\"bikename\":\"lvju\",\
		\"gps\":\"024813.640,A,3158.4608,N,11848.3737,E,10.05,324.27,150706\",\
		\"time\":\"2017-5-10-17:10:20\"\
	}\
	}"};

	Json::Reader reader;
	Json::Value json_object;
	if (!reader.parse(json_str, json_object))
	return 0;
	//std::cout << json_object["error"] << std::endl;
	//std::cout << json_object["data"]["token"] << std::endl;    
	//std::cout << json_object["data"]["card"] << std::endl;

	//json_object["data"]["shao"] = Json::Value("value_string"); 

	//std::cout << json_object["data"]["shao"] << std::endl;

	std::cout << json_object << std::endl;

	MakeUUID();//生成UUID
	//////////////////////////////////////////////////////////////////
	
	//主线程执行其他工作。
	WorkDatathreadpool.CreatWorkDataThread(10);//创建解包线程
	SaveDatathreadpool.CreatSaveDataThread(20);//创建存储线程
	//APPthreadpool.CreatAPPDataThread(10);//创建APP解包线程

	//////////////////////////////////////////////////////////////////
	IOCPsocket.Create("" , MYPORT , true);//创建SOCKET服务端，针对硬件连接
	//APPsocket.Create("" ,APP_PORT ,true); //创建socket服务，针对APP连接

	
	IOCPsocket.Listen(10);
	//APPsocket.Listen(10);

	// 创建用于发送数据的线程
	HANDLE acceptThread = (HANDLE)_beginthreadex(NULL, 0, (unsigned int (__stdcall *)(void *))socket_accept_thread, pst2, 0, 0); 
	//HANDLE APPacceptThread = (HANDLE)_beginthreadex(NULL, 0, (unsigned int (__stdcall *)(void *))APP_accept_thread, pst3, 0, 0); 

	//RecEvent[0]=CreateEvent(NULL, FALSE, FALSE, NULL);  
	WX_CheckAlarmEvent[0] = CreateEvent(NULL, FALSE, FALSE, NULL);//创建事件，用于微信查询报警列表推送模板消息
	WX_GetTokenEvent[0] = CreateEvent(NULL, FALSE, FALSE, NULL);//创建事件,用于获取微信TOKEN

	memset(m_SocReUnit.RecData ,0,m_SocReUnit.DataLen);//接收缓存清0
	
	InitializeCriticalSection(&m_RECdataLock);//初始化硬件接收数据锁
	//InitializeCriticalSection(&mAPP_RECLock);//初始化APP接收数据锁
	InitializeCriticalSection(&mSaveDataLock);//初始化硬件上传的数据存储线程互斥锁
	InitializeCriticalSection(&card_list_Lock);//card列表操作锁
	
	HANDLE WX_AlarmThread = (HANDLE)_beginthreadex(NULL, 0, (unsigned int (__stdcall *)(void *))WX_CheckAlarmThread, pst4, 0, 0); 
	HANDLE WX_TokenThread = (HANDLE)_beginthreadex(NULL, 0, (unsigned int (__stdcall *)(void *))WX_GetTokenThread, pst5, 0, 0); 

	//////////////////////////////////////////////////////////////////////////
	while(1)
	{
		printf("等待输入命令：!\n");//
		printf("--------save--------保存数据为jpg文件：!\n");//
		printf("--------total-------显示总接收字节数：!\n");//
		printf("--------exit--------程序退出：!\n");//
		printf("--命令---：");//
		scanf("%s" ,inputcmd);
		if(strstr(inputcmd,"save"))
		{						
			time(&rawtime);//获取当前时间
			curTime = localtime ( &rawtime );
			sprintf(filename,"%04d-%02d-%02d-%02d-%02d-%02d.jpg",curTime->tm_year+1900,
			curTime->tm_mon+1,curTime->tm_mday,curTime->tm_hour,curTime->tm_min,
			curTime->tm_sec);
			printf("%s",filename);
			fpin=fopen(filename,"wb");//以写的方式打开文件
			if(!fpin)//判断文件打开是否成功
			{
				printf("file creat wrong\n");
				exit(1);
			}
			fwrite (card_list.c_str() , sizeof( char), card_list.length() , fpin);

			printf("文件写入完成!\n");//写入完成提示
			fclose(fpin);//关闭文件
			printf("jpg文件保存成功!\n");//
		}

		if(strstr(inputcmd,"total"))
		{
			printf("全部接收字节数为：%d\r\n" , totalcnt);
		}
		if(strstr(inputcmd,"exit"))
		{
			system("pause");
			break;
		}
	}

	return 0;

}
////////////////////发送邮件SMTP方式SSL加密链接//////////////////////////////
int  Email_SMTP_send_simple(string  smtp_server , string  SenderEmail ,string SenderAccount , string SenderPWD, string RecEmail , Json::Value json_email)
{
	bool bError = false;

	try
	{
		CSmtp mail;
		mail.SetCharSet("gb2312");//设置编码

	#define  test_gmail_ssl

	#if defined(test_gmail_tls)
		mail.SetSMTPServer("smtp.gmail.com", 587);
		mail.SetSecurityType(USE_TLS);
	#elif defined(test_gmail_ssl)
		mail.SetSMTPServer(smtp_server.c_str(), 465);
		mail.SetSecurityType(USE_SSL);
	#elif defined(test_hotmail_TLS)
		mail.SetSMTPServer("smtp.live.com", 25);
		mail.SetSecurityType(USE_TLS);
	#elif defined(test_aol_tls)
		mail.SetSMTPServer("smtp.aol.com", 587);
		mail.SetSecurityType(USE_TLS);
	#elif defined(test_yahoo_ssl)
		mail.SetSMTPServer("plus.smtp.mail.yahoo.com", 465);
		mail.SetSecurityType(USE_SSL);
	#endif

		mail.SetLogin(SenderAccount.c_str());
		mail.SetPassword(SenderPWD.c_str());
		mail.SetSenderName("User");
		mail.SetSenderMail(SenderEmail.c_str());
		mail.SetReplyTo(SenderEmail.c_str());
		mail.SetSubject("报警通知");
		mail.AddRecipient(RecEmail.c_str());
		mail.SetXPriority(XPRIORITY_NORMAL);
		mail.SetXMailer("The Bat! (v3.02) Professional");
		//mail.AddMsgLine("Hello,");
		//mail.AddMsgLine("");
		//mail.AddMsgLine("...");
		//mail.AddMsgLine("How are you today?");
		//string  gbk_str = "您的设备报警了！请及时查看！" ;
		mail.AddMsgLine("您的设备报警了！请及时查看！");
		//mail.AddMsgLine("Regards");
		//mail.ModMsgLine(5, "少占鱼");
		///mail.DelMsgLine(2);
		
		string  gbk_str = "报警设备是： " + UTF8ToGBK(json_email["bikename"].asString().c_str());

		mail.AddMsgLine(gbk_str.c_str() );
		gbk_str = "报警状态： " + UTF8ToGBK(json_email["state"].asString().c_str());
		mail.AddMsgLine(gbk_str.c_str());
		gbk_str = "报警时间： " + UTF8ToGBK(json_email["alarm_time"].asString().c_str());
		mail.AddMsgLine(gbk_str.c_str());
		//mail.ModMsgLine(2, email_content.c_str());
		//mail.DelMsgLine(2);
		//mail.AddMsgLine("User");

		//mail.AddAttachment("../test1.jpg");
		//mail.AddAttachment("c:\\test2.exe");
		//mail.AddAttachment("c:\\test3.txt");
		mail.SetCharSet("gb2312");
		mail.Send();//启动发送
	}
	catch (ECSmtp e)
	{
		std::cout << "Error: " << e.GetErrorText().c_str() << ".\n";
		bError = true;
		//system("pause");
	}
	if (!bError)
	{
		std::cout << "Mail was send successfully.\n";
		//system("pause");
	}

	return 0;
}

////////////////////////////////////////////////////
#include "stdafx.h"
#include "HDPrinter.h"
//#include "spl.h"
#include "HDDownload.h"
#include "ResultThread.h"
#include "HDDataCenter.h"
#include "DistributeThread.h"
#include "tinyxml.h"
#include <bitset>
#include <fstream>
#include <direct.h>
#include "mupdf_wrapper.h"

#define CRTDBG_MAP_ALLOC
#include <stdlib.h>
#include <crtdbg.h>
#include <string.h>
#define EMF_FILETYPE 0
#define PDF_FILETYPE 1
#define BMP_FILETYPE 2
#define BIG_FILETYPE 3

extern int Njimi;
extern int NHXshangmi;
extern int Nmimi;
extern int NPTshangmi;
extern int Nneibu;
extern int Nfeimi;extern int NprintNum;
int NprinterNum=0;

/*以下该模块是完成BMP图像(彩色图像是24bit RGB各8bit)的像素获取*/
unsigned char *pBmpBuf;             //读入图像数据的指针
int bmpWidth = 0;                       //图像的宽
int bmpHeight = 0;                      //图像的高
//RGBQUAD *pColorTable;               //颜色表的指针
int biBitCount = 0;                     //每像素位数
DWORD       structdevmodeSize;//打印机devmode大小；
typedef  int (*pEMFWatermarkEmbed)(char* srcEMFFilePath, char* desEMFFilePath, char* chWatermarkInfoMsg,char* szPassword,char* szPrinterName, int nBitsType,bool bCompanyIdentify);





CHDPrinter::CHDPrinter(PrinterInfo printInfo)
	: m_strPrinterName(printInfo.szPrinterName)
	, m_strPrinterPath(printInfo.szPrinterPath)
	, m_strPrinterID(printInfo.szPrinterID)
	, m_nPrinterType(printInfo.nPrinterSeclvCode)
	, m_dwPrintThreadID(0)
	, m_dwWatchThreadID(0)
	, m_hPrintStopEvent(NULL)
	, m_hWatchStopEvent(NULL)
	, m_hPrintStoppedEvent(NULL)
	, m_hWatchStoppedEvent(NULL)
	, m_nLastPageSize(0)
	, m_nLastOrientation(0)
	, m_nBarcodeType(0)
	,m_nFileType(3)
	,m_pEngine(NULL)
	,m_nPrintedCounts(0)
	,m_nLastPAPERSIZE(0)
{
	m_HDAppConfig = HDAppConfig::Instance();
	m_piocp = HDIOCP::Instance();


	for (int i = 0; i < m_listJob.GetCount(); i++)
	{
		delete m_listJob.GetAt(i);
	}
	m_listJob.RemoveAll();
	m_listJob.SetSize(0);

	for (int i = 0; i < m_listSendedJob.GetCount(); i++)
	{
		delete m_listSendedJob.GetAt(i);
	}
	m_listSendedJob.RemoveAll();
	m_listSendedJob.SetSize(0);

	for (int i = 0; i < m_listSendedInfo.GetCount(); i++)
	{
		delete m_listSendedInfo.GetAt(i);
	}
	m_listSendedInfo.RemoveAll();
	m_listSendedInfo.SetSize(0);

#ifdef SECURITY_LEVEL_VALID
	m_strSecAllowed.Format(_T("%s"), printInfo.szAllowSecInfo);
#endif

	TCHAR szPrint[MAX_PATH] = {0x00};
	TCHAR szWatch[MAX_PATH] = {0x00};
	TCHAR szPrintting[MAX_PATH] = {0x00};
	TCHAR szResult[MAX_PATH] = {0x00};
	sprintf(szPrint, "%sPrint", printInfo.szPrinterName);
	sprintf(szWatch, "%sWatch", printInfo.szPrinterName);
	sprintf(szPrintting, "%sPrintting", printInfo.szPrinterName);
	sprintf(szResult, "%sResult", printInfo.szPrinterName);

	m_hPrintStopEvent = CreateEvent(NULL, TRUE, FALSE, szPrint);
	if (NULL == m_hPrintStopEvent)
	{
		GenLog(ERROR_INFO, "%s[%d] 打印机%s生成打印线程停止事件失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
		m_strPrinterName.ReleaseBuffer();
	}

	m_hWatchStopEvent = CreateEvent(NULL, TRUE, FALSE, szWatch);
	if (NULL == m_hWatchStopEvent)
	{
		GenLog(ERROR_INFO, "%s[%d] 打印机%s生成打印线程停止事件失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
		m_strPrinterName.ReleaseBuffer();
	}

	m_hPrintStoppedEvent = CreateEvent(NULL, TRUE, FALSE, szPrintting);
	if (NULL == m_hPrintStoppedEvent)
	{
		GenLog(ERROR_INFO, "%s[%d] 打印机%s生成打印线程停止事件失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
		m_strPrinterName.ReleaseBuffer();
	}

	m_hWatchStoppedEvent = CreateEvent(NULL, TRUE, FALSE, szResult);
	if (NULL == m_hWatchStoppedEvent)
	{
		GenLog(ERROR_INFO, "%s[%d] 打印机%s生成打印线程停止事件失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
		m_strPrinterName.ReleaseBuffer();
	}

	// 初始化doc
	/*if(!AfxOleInit())
	{
	AfxMessageBox(_T("无法初始化COM的动态链接库"));
	return;
	}*/
}

CHDPrinter::~CHDPrinter()
{
	SetEvent(m_hPrintStopEvent);
	SetEvent(m_hWatchStopEvent);
	WaitForSingleObject(m_hPrintStoppedEvent, INFINITE);
	WaitForSingleObject(m_hWatchStoppedEvent, INFINITE);
	Sleep(200);

	CloseHandle(m_hPrintStopEvent);
	CloseHandle(m_hWatchStopEvent);
	CloseHandle(m_hPrintStoppedEvent);
	CloseHandle(m_hWatchStoppedEvent);

	m_csListLock.Lock();
	for (int i = 0; i < m_listJob.GetCount(); i++)
	{
		//是否要判断状态,要不要等解锁回包？
		//if (m_listJob.GetAt(i)->m_JobStatusInfo.m_nStaus == WAITTING)
		{
			// 删除向服务器发送解锁包 [7/15/2014 Administrator]
			//m_piocp->UnlockJob(PRINT, m_listJob.GetAt(i)->m_PrintJobInfo.szEventCode, strlen(m_listJob.GetAt(i)->m_PrintJobInfo.szEventCode));
			//DWORD dwStatus = WaitForSingleObject(m_piocp->m_hUnlockEvent, 5*1000);
			//int nTime = 0;
			//while (dwStatus != WAIT_OBJECT_0)
			//{
			//	m_piocp->UnlockJob(PRINT, m_listJob.GetAt(i)->m_PrintJobInfo.szEventCode, \
			//		strlen(m_listJob.GetAt(i)->m_PrintJobInfo.szEventCode));

			//	dwStatus = WaitForSingleObject(m_piocp->m_hUnlockEvent, 5*1000);

			//	nTime++;
			//	if (nTime >= 3)
			//	{
			//		CString strMsg;
			//		strMsg.Format(_T("解锁文件%s失败！"), m_listJob.GetAt(i)->m_PrintJobInfo.szEventCode);
			//		ShowTipMsg(strMsg.GetBuffer(0), c_btnDelayTime);
			//		GenLog(ERROR_INFO, "%s[%d].%s\n",__FILE__,__LINE__, strMsg.GetBuffer(0));
			//		break;
			//	}
			//}
			delete m_listJob.GetAt(i);
		}
	}
	m_listJob.RemoveAll();
	m_listJob.SetSize(0);

	ClearSendedJob();

	for (int i = 0; i < m_listSendedInfo.GetCount(); i++)
	{
		GenLog(ERROR_INFO, "%s[%d].未检测到文件%s条码%s的打印结果！\n", __FILE__, __LINE__, \
			m_listSendedInfo.GetAt(i)->szPrintJobID, m_listSendedInfo.GetAt(i)->szBarcode);
		delete m_listSendedInfo.GetAt(i);
	}
	m_listSendedInfo.RemoveAll();
	m_listSendedInfo.SetSize(0);

	m_csListLock.Unlock();
}

BOOL CHDPrinter::StartPrintThread(void)
{
	BOOL bRet = FALSE;

	HANDLE hThread = ::CreateThread(NULL, NULL, PrintThread, (LPVOID)this, NULL, &m_dwPrintThreadID);
	if (hThread == NULL)
	{
		GenLog(ERROR_INFO, "%s[%d].打印机%s启动打印线程失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
	}
	else
	{
		bRet = TRUE;
	}

	return bRet;
}

BOOL CHDPrinter::StartMonitorPrinterThread(PVOID szPrinterID)
{
	BOOL bRet = FALSE;

	HANDLE hThread = ::CreateThread(NULL, NULL, MonitorPrinterThread, (PVOID)szPrinterID, NULL, &ThreadID);
	if (hThread == NULL)
	{
		GenLog(ERROR_INFO, "%s[%d] 打印机%s启动打印线程失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
		m_strPrinterName.ReleaseBuffer();
	}
	else
	{
		bRet = TRUE;
	}
	return bRet;
}

//将打印机ID添加到打印监控列表中
BOOL CHDPrinter::AddprinterMonitorList(PVOID szID)
{
	char lpPrinterId[256] = {0x00};
	char* p;
	p=(char *)szID;
	sprintf(lpPrinterId ,"%s" , p);

	PrinterTaskInfo* pPrinter = new PrinterTaskInfo;
	memcpy(pPrinter->cPrinterCode , lpPrinterId , strlen(lpPrinterId));
	CHDDataCenter::Instance()->m_PrinterTaskInfo.InsertAt(CHDDataCenter::Instance()->m_PrinterTaskInfo.GetCount(), pPrinter);
	//delete pPrinter;

	return TRUE;
}

BOOL CHDPrinter::StartWatchThread(void)
{
	BOOL bRet = FALSE;

	HANDLE hThread = ::CreateThread(NULL, NULL, WatchThread, (LPVOID)this, NULL, &m_dwWatchThreadID);
	if (hThread == NULL)
	{
		GenLog(ERROR_INFO, "%s[%d] 打印机%s启动打印线程失败，原因：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0), GetErrorMessage());
		m_strPrinterName.ReleaseBuffer();
	}
	else
	{
		bRet = TRUE;
	}

	return bRet;
}

BOOL CHDPrinter::InsertJobList(PrintJob* printJob)
{
	BOOL bRet = TRUE;

	PrintJob* tmpJob = new PrintJob();

	CopyPrintJob(tmpJob, printJob);

	m_csListLock.Lock();
	m_listJob.InsertAt(m_listJob.GetUpperBound()+1, tmpJob);
	m_csListLock.Unlock();

	return bRet;
}

PrintJob* CHDPrinter::GetFirstJob(void)
{
	// 使用期间都需锁住 [3/7/2014 Administrator]
	m_csListLock.Lock();
	PrintJob* pJob = NULL;
	if (m_listJob.GetUpperBound() != -1)
	{
		pJob = (PrintJob*)m_listJob.GetAt(0);
	}
	m_csListLock.Unlock();

	return pJob;
}



PrintJob* CHDPrinter::FindUnSendJob(void)
{
	PrintJob* tmpJob = NULL;

	for (int n = 0; n < m_listJob.GetCount(); n++)
	{
		PrintJob* FindJob = NULL;
		FindJob = m_listJob.GetAt(n);
		if (FindJob->m_bIsSend == FALSE)
		{
			tmpJob = (PrintJob*)m_listJob.GetAt(n);

			break;
		}
	}

	return tmpJob;
}


int CHDPrinter::GetUnSendJobCount(void)
{
	int nCount = 0;
	PrintJob* tmpJob = NULL;

	for (int n = 0; n < m_listJob.GetCount(); n++)
	{
		PrintJob* FindJob = NULL;
		FindJob = m_listJob.GetAt(n);
		if (FindJob->m_bIsSend == FALSE)
		{
			nCount ++;

		}
	}

	return nCount;
}


PrintJob* CHDPrinter::SetUnSendJob(void)
{
	PrintJob* tmpJob = NULL;

	for (int n = 0; n < m_listJob.GetCount(); n++)
	{
		PrintJob* FindJob = NULL;
		FindJob = m_listJob.GetAt(n);
		if (FindJob->m_bIsSend == FALSE)
		{
			tmpJob = (PrintJob*)m_listJob.GetAt(n);
			tmpJob->m_bIsSend = TRUE;
			m_listJob.SetAt(n, tmpJob);

			break;
		}
	}

	return tmpJob;
}


BOOL CHDPrinter::DeleteFirstJob(void)
{
	BOOL bRet = TRUE;

	m_csListLock.Lock();
	if (m_listJob.GetUpperBound() != -1)
	{
		PrintJob* pJob = (PrintJob*)m_listJob.GetAt(0);
		delete pJob;
		pJob = NULL;
		m_listJob.RemoveAt(0);
		// 确保内存和list内指针删除 [3/7/2014 Administrator]
		m_listJob.SetSize(m_listJob.GetCount());
	}
	m_csListLock.Unlock();

	return bRet;
}

BOOL CHDPrinter::CheckFileType(int nFileType)
{
	BOOL bRet = TRUE;
	bRet = CHDDataCenter::Instance()->IsAllowHere(nFileType);

	return bRet;
}

int CHDPrinter::GetJobNum()
{
	int nNum = 0;

	m_csListLock.Lock();
	nNum = m_listJob.GetCount();
	m_csListLock.Unlock();

	return nNum;
}

int CHDPrinter::GetSendedJobNum()
{
	int nNum = 0;

	m_csListLock.Lock();
	nNum = m_listSendedJob.GetCount();
	m_csListLock.Unlock();

	return nNum;
}

int CHDPrinter::GetSendedJobInfoNum()
{
	int nNum = 0;

	m_csListLock.Lock();
	nNum = m_listSendedInfo.GetCount();
	m_csListLock.Unlock();

	return nNum;
}

DWORD CHDPrinter::GetPrintThreadID(void)
{
	return this->m_dwPrintThreadID;
}

DWORD CHDPrinter::GetWatchThreadID(void)
{
	return this->m_dwWatchThreadID;
}

TCHAR* CHDPrinter::GetPrinterID(void)
{
	return m_strPrinterID.GetBuffer(0);
}

TCHAR* CHDPrinter::GetPrinterPath(void)
{
	return m_strPrinterPath.GetBuffer(0);
}

TCHAR* CHDPrinter::GetPrinterName(void)
{
	return m_strPrinterName.GetBuffer(0);
}

BOOL CHDPrinter::DownloadAndParse(PrintJob* pCurrInfo)
{
	if(pCurrInfo)
	{
		if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
		{
			if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
			{
				// 【刷卡器显示】 [9/6/2015 haojia]
				TCHAR cShowMsg[MAX_PATH] = {0x00};
				int nLen = 0;
				strcpy(cShowMsg,_T("正在下载..."));			
				nLen = strlen(cShowMsg);
				CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
			}
		}

		// 增加进度条 [4/23/2020 Administrator]
		if (SILENCE_SCREEN != HDAppConfig::Instance()->m_AppConfig.m_nDisplayMode)
		{
			ShowTipBox(_T("打印作业下载处理中，请耐心等待"));
		}
		TCHAR filepath[MAX_PATH*2] = {0x00};//服务器压缩包的路径
		TCHAR szUnippedpath[MAX_PATH*2] = {0x00};//解压缩的路径
#ifdef PRINTBURN_PATH
		if(strlen(pCurrInfo->m_PrintJobInfo.cFilePathStoreTime)==0)
		{
			sprintf(filepath,"http://%s:%d/cclcm/files/print/%s", m_HDAppConfig->m_AppConfig.m_strServerIP.GetBuffer(0), m_HDAppConfig->m_AppConfig.m_nWebPort,
				pCurrInfo->m_PrintJobInfo.szZipFileName);
		}
		else
		{
			char year[6]={0};
			char month[6]={0};
			memcpy(year,pCurrInfo->m_PrintJobInfo.cFilePathStoreTime,4);
			strcpy(month,pCurrInfo->m_PrintJobInfo.cFilePathStoreTime+4);
			sprintf(filepath,"http://%s:%d/cclcm/files/print/%s/%s/%s", m_HDAppConfig->m_AppConfig.m_strServerIP.GetBuffer(0), m_HDAppConfig->m_AppConfig.m_nWebPort,year,month,
				pCurrInfo->m_PrintJobInfo.szZipFileName);
		}
		GenLog(DEBUG_INFO, "%s[%d].文件%s下载地址：%s\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode, filepath);
#else
		sprintf(filepath,"http://%s:%d/cclcm/files/print/%s", m_HDAppConfig->m_AppConfig.m_strServerIP.GetBuffer(0), m_HDAppConfig->m_AppConfig.m_nWebPort,
			pCurrInfo->m_PrintJobInfo.szZipFileName);
		GenLog(DEBUG_INFO, "%s[%d].文件%s下载地址：%s\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode, filepath);
#endif
		// 获取文件大小
		LONGLONG filesize = GetSingleFileSize(filepath, HDAppConfig::Instance()->m_AppConfig.m_nWebPort);
		GenLog(DEBUG_INFO, "%s[%d].文大小：%d断口：%d\n", __FILE__, __LINE__, filesize, HDAppConfig::Instance()->m_AppConfig.m_nWebPort);

		if (filesize <= 0)
		{
			//下载大小 小于等于0时肯定有问题，不用继续运行
			// 网络模式 [4/14/2015 chenhong]
			if (HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel == WORKING_NETWORK)
			{
				if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
				{
					// 【刷卡器显示】 [9/6/2015 haojia]
					TCHAR cShowMsg[MAX_PATH] = {0x00};
					int nLen = 0;
					strcpy(cShowMsg,_T("下载失败..."));			
					nLen = strlen(cShowMsg);
					CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
				}
			}
			else
			{
				ShowMsgBox(_T("原文件不存在，请重新提交任务"),MB_OK);
			}
			GenLog(ERROR_INFO, "%s[%d].文件%s下载失败，由于文件大小<=0\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
			return FALSE;
		}
		if (SILENCE_SCREEN != HDAppConfig::Instance()->m_AppConfig.m_nDisplayMode)
		{
			SetTipBoxProg(30);
		}
		CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_DOWNLOAD_TOTALSIZE, NULL, (LPARAM)filesize);
		GenLog(DEBUG_INFO, "%s[%d].[===========================文件%s正在下载===============]\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
		if (1 == DownloadFile(CHDDataCenter::Instance()->GetDirectory(0), filepath, HDAppConfig::Instance()->m_AppConfig.m_nWebPort))
		{
			GenLog(DEBUG_INFO, "%s[%d].[========================文件%s下载成功=============]\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
			//GetDefaultEncryptZipPath(filepath,_MAX_PATH);		//获取的C:\Documents and Settings\All Users\Application Data\printcache\			

			memset(filepath, 0x00, MAX_PATH*2*sizeof(char));
			strcat(filepath, CHDDataCenter::Instance()->GetDirectory(0));
			strcat(filepath, pCurrInfo->m_PrintJobInfo.szZipFileName);

			if (SILENCE_SCREEN != HDAppConfig::Instance()->m_AppConfig.m_nDisplayMode)
			{
				SetTipBoxProg(70);
			}
			//下载之后先初始化文件链表
			UnzipListInit(pCurrInfo);
			GenLog(DEBUG_INFO, "%s[%d].[=================文件%sDownloadAndParse正在解压=====================]\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
			int unzipStatus = this->ParseZip(filepath, sizeof(filepath), szUnippedpath, sizeof(szUnippedpath), pCurrInfo);  //return 0;成功   非0失败
			//SetTipBoxProg(100);
			if (SILENCE_SCREEN != HDAppConfig::Instance()->m_AppConfig.m_nDisplayMode)
			{
				SetTipBoxProg(100);
			}
			if (0 == unzipStatus)
			{
				GenLog(DEBUG_INFO, "%s[%d].文件%sDownloadAndParse解压成功：%s\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode, filepath);
				return TRUE;
			}
			else//解密解压不成功
			{
				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
				{
					if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
					{
						// 【刷卡器显示】 [9/6/2015 haojia]
						TCHAR cShowMsg[MAX_PATH] = {0x00};
						int nLen = 0;
						strcpy(cShowMsg,_T("解密解压失败"));			
						nLen = strlen(cShowMsg);
						CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
					}
				}
				//GenLog(ERROR_INFO,"%s[%d].文件%sunzipStatus = %d\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode, unzipStatus);
				return FALSE;
			}
		}
		else//下载不成功
		{
			return FALSE;
		}
	}

	return FALSE;
}

char* CHDPrinter::GetSID(char *sid)
{
	char userName[260] = "";
	//char sid[260] = "";
	DWORD nameSize = sizeof(userName);
	GetUserName((LPTSTR)userName,&nameSize);
	char userSID[260] = "";
	char userDomain[260] = "";
	DWORD sidSize = sizeof(userSID);
	DWORD domainSize = sizeof(userDomain);
	SID_NAME_USE snu;
	LookupAccountName(NULL,
		(LPTSTR)userName,
		(PSID)userSID,
		&sidSize,
		(LPTSTR)userDomain,
		&domainSize,
		&snu);
	PSID_IDENTIFIER_AUTHORITY psia = GetSidIdentifierAuthority(userSID);
	sidSize = sprintf(sid, "S-%lu-",SID_REVISION);
	sidSize += sprintf(sid +strlen(sid),"%-lu",psia->Value[5]);
	int i = 0;
	int subAuthorities = *GetSidSubAuthorityCount(userSID);
	for (i = 0;i < subAuthorities; i++)
	{
		sidSize += sprintf(sid + sidSize,"-%lu",*GetSidSubAuthority(userSID,i));
	}
    return sid;
}

//解密解压压缩文件，同时将解压后的文件形成链表，存于PrintJob结构
int CHDPrinter::ParseZip(char* src,int srclen,char* dst,int dstlen, PrintJob* pCurrInfo)
{
	int status = 0;
	char drive[3] = {0x00};     
	char dir[256] = {0x00};    
	char fname[260] = {0x00};     
	char ext[256] = {0x00}; 
	char olddir[260] = {0x00};
	char* filename = NULL;
	TCHAR szEvenCode[MAX_PATH] = {0x00};
	BOOL bIsZip = FALSE;
	BOOL bIsBigFile = FALSE;
	char tmp[260]={0x00};
	GenLog(DEBUG_INFO, "%s[%d].[ParseZip ：%s]\n", __FILE__, __LINE__,src);
	GenLog(DEBUG_INFO, "%s[%d].[ParseZip ：%s]\n", __FILE__, __LINE__,dst);

	CTime ct = CTime::GetCurrentTime();
	int ctnow = ct.GetTime();
	GenLog(DEBUG_INFO, "%s[%d].[ParseZip in ]\n", __FILE__, __LINE__);
	sprintf_s(szEvenCode, MAX_PATH, _T("%s%s%d\\"), CHDDataCenter::Instance()->GetDirectory(1), pCurrInfo->m_PrintJobInfo.szEventCode,ctnow);
	MakeSureDirectoryPathExists(szEvenCode);
	GenLog(DEBUG_INFO, "%s[%d].[MakeSureDirectoryPathExists%s]\n", __FILE__, __LINE__,szEvenCode);
	memset(szEvenCode, 0x00, sizeof(szEvenCode));
	sprintf_s(szEvenCode, MAX_PATH, _T("%s%d\\"), pCurrInfo->m_PrintJobInfo.szEventCode,ctnow);
	GenLog(DEBUG_INFO, "%s[%d].[MakeSureDirectoryPathExists%s]\n", __FILE__, __LINE__,szEvenCode);
	char decryptZipPath[MAX_PATH] = {0x00};
	sprintf(decryptZipPath, "%s%s%s", CHDDataCenter::Instance()->GetDirectory(1), szEvenCode, pCurrInfo->m_PrintJobInfo.szZipFileName);
	char decryptpath[MAX_PATH] = {0x00};
	sprintf(decryptpath, "%s%s%d", CHDDataCenter::Instance()->GetDirectory(1), pCurrInfo->m_PrintJobInfo.szEventCode,ctnow);
	// 网络模式 [4/14/2015 chenhong]
	if (HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel == WORKING_NETWORK)
	{
		if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
		{
			// 【刷卡器显示】 [9/6/2015 haojia]
			TCHAR cShowMsg[MAX_PATH] = {0x00};
			int nLen = 0;
			strcpy(cShowMsg,_T("开始解密文件..."));			
			nLen = strlen(cShowMsg);
			CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
		}
	}

	_splitpath(decryptZipPath, drive, dir, fname, ext);
	GenLog(DEBUG_INFO, "%s[%d].[_splitpath %s]\n", __FILE__, __LINE__,decryptZipPath);
	// 匹配PS or PDF
	if(strcmp(ext,".ps")==0)
	{
		bIsZip = FALSE;
		sprintf(decryptZipPath,"%s%s%s%s",drive, dir, fname, ".pdf");
	}
	else if(strcmp(ext,".zip")==0)
	{
		bIsZip = TRUE;
	}
	else if(strcmp(ext, ".hdzip") == 0)
	{
		bIsZip = TRUE;
		bIsBigFile = TRUE;
	}
	GenLog(DEBUG_INFO, "%s[%d].[ext 文件类型：%s]\n", __FILE__, __LINE__,ext);
	GenLog(DEBUG_INFO, "%s[%d].[ext bIsZip：%d]\n", __FILE__, __LINE__,bIsZip);
	GenLog(DEBUG_INFO, "%s[%d].[ext bIsBigFile：%d]\n", __FILE__, __LINE__,bIsBigFile);

	int nEncy = 0;
	if(bIsBigFile == TRUE)
	{
		nEncy = Encrypt(src, decryptZipPath);
	}
	else
	{
		GenLog(DEBUG_INFO, "%s[%d].[ src：%s]\n", __FILE__, __LINE__,src);
		GenLog(DEBUG_INFO, "%s[%d].[ decryptZipPath：%s]\n", __FILE__, __LINE__,decryptZipPath);
		nEncy = HangDunCryptographyFile(DECRYPTION, ALG_RC4, src, decryptZipPath);
		GenLog(DEBUG_INFO, "%s[%d].[航盾解密成功 ：%d]\n", __FILE__, __LINE__,nEncy);
	}
	GenLog(DEBUG_INFO, "%s[%d].[航盾解密成功 ：%s]\n", __FILE__, __LINE__,ext);
	//解密压缩包
	if (nEncy == 0)
	{
	}
	else
	{
		// 网络模式 [4/14/2015 chenhong]
		if (HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel == WORKING_NETWORK)
		{
			if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
			{
				// 【刷卡器显示】 [9/6/2015 haojia]
				TCHAR cShowMsg[MAX_PATH] = {0x00};
				int nLen = 0;
				strcpy(cShowMsg,_T("文件解密失败..."));			
				nLen = strlen(cShowMsg);
				CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
			}
		}

		GenLog(ERROR_INFO, "%s[%d].解密文件%s失败！\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szFileName);
		DeleteFile(src);
		return -1;
	}
	GenLog(DEBUG_INFO, "%s[%d].[航盾解密成功 ：%d]\n", __FILE__, __LINE__,nEncy);
	GenLog(DEBUG_INFO, "%s[%d].[DeleteFile ：%d]\n", __FILE__, __LINE__,src);
	DeleteFile(src);
	GenLog(DEBUG_INFO, "%s[%d].[DeleteFile ：%d]\n", __FILE__, __LINE__,src);
	char unzippath[MAX_PATH] = {0x00};
	memset(unzippath, 0x00, 260);
	if(bIsZip)
	{
		if (IsWin7())
		{
			char sidname[260] = {0x00};
			GetSID(sidname);
			if(PathIsDirectory("C:\\Recycled"))
			{
				GenLog(DEBUG_INFO, "%s[%d].recycled文件夹存在！\n", __FILE__, __LINE__);
				sprintf(unzippath, "%s%s", "C:\\Recycled\\%s\\%s", sidname, szEvenCode);
			}
			else
			{
				GenLog(DEBUG_INFO, "%s[%d].recycled文件夹不存在！\n", __FILE__, __LINE__);
				sprintf(unzippath,  "C:\\$Recycle.Bin\\%s\\%s", sidname, szEvenCode);			
			}
		} 
		else
		{
			sprintf(unzippath, "%s%s", drive, dir);
		}	
	}
	else
	{
		sprintf(unzippath, "%s%s", drive, dir);
	}

	GenLog(DEBUG_INFO, "%s[%d].解压路径unzippath %s ！\n", __FILE__, __LINE__, unzippath);
	// 网络模式 [4/14/2015 chenhong]
	if (HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel == WORKING_NETWORK)
	{
		if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
		{
			// 【刷卡器显示】 [9/6/2015 haojia]
			TCHAR cShowMsg[MAX_PATH] = {0x00};
			int nLen = 0;
			strcpy(cShowMsg,_T("开始解压文件..."));			
			nLen = strlen(cShowMsg);
			CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
		}
	}

	if(bIsZip)
	{
		//status = UnZip(decryptZipPath, unzippath);
		
		GenLog(DEBUG_INFO, "%s[%d].解压路径decryptZipPath %s ！\n", __FILE__, __LINE__, decryptZipPath);		
		GenLog(DEBUG_INFO, "%s[%d].解压路径unzippath %s ！\n", __FILE__, __LINE__, unzippath);
		//解压文件
			
		TCHAR szFullPath[MAX_PATH] = {0x00};	//用于存储获取当前程序全路径
		TCHAR szDir[MAX_PATH] = {0x00};
		TCHAR szDrive[MAX_PATH] = {0x00};
		char m_InstallPath[MAX_PATH] = {0x00};
		TCHAR szConfExPath[MAX_PATH] = {0x00};
		::GetModuleFileName(NULL, szFullPath, MAX_PATH);
		_tsplitpath(szFullPath, szDrive, szDir, NULL, NULL);
		sprintf_s(m_InstallPath, MAX_PATH, "%s%s", szDrive, szDir);
		int nAdd = strlen(m_InstallPath);
		if(m_InstallPath[nAdd-1] == '\\')
		{
		sprintf_s(m_InstallPath, MAX_PATH, "%s", m_InstallPath);
		}
		else
		{
		sprintf_s(m_InstallPath, MAX_PATH, "%s\\", m_InstallPath);
		}
		sprintf_s(szConfExPath, MAX_PATH, _T("%s7z.exe"), m_InstallPath);
		CString url_86;
		STARTUPINFO si_86;
		PROCESS_INFORMATION pi_86;

		ZeroMemory( &si_86, sizeof(si_86) );
		si_86.cb = sizeof(si_86);
		ZeroMemory( &pi_86, sizeof(pi_86) );
		url_86.Format(_T("\"%s\" x \"%s\" -o\"%s\""), szConfExPath, decryptZipPath,unzippath);

		int nCreProRet = CreateProcess(NULL, url_86.GetBuffer(0), NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		CREATE_NO_WINDOW,              // No creation flags
		NULL,           // Use parent's environment block
		NULL, 
		&si_86,
		&pi_86 );

		GenLog(ERROR_INFO, "%s[%d] nCreProRet %d \n", __FILE__, __LINE__, nCreProRet);
		if (nCreProRet == 0)
		{
		// 失败
		int nGetLastError = GetLastError();
		GenLog(ERROR_INFO, "%s[%d] 文件解压失败,Last error = %d \n", __FILE__, __LINE__, nGetLastError);
		}
		WaitForSingleObject(pi_86.hProcess, INFINITE);		// 等待子进程的退出		
		CloseHandle(pi_86.hThread);		
		CloseHandle(pi_86.hProcess); 
		DeleteFile(decryptZipPath);
		RemoveDirectoryA(decryptpath);
		int temp = DeleteFile(decryptZipPath); //解压成功后删除压缩包unzipprint/xxxxxx.zip文件
		sprintf(tmp,"%s%s",unzippath,fname);
		memcpy(unzippath,tmp,260);
		GenLog(DEBUG_INFO, "%s[%d].解压路径unzippath %s ！\n", __FILE__, __LINE__, unzippath);
	}
	status==0;
	if(status == 0)
	{
		WIN32_FIND_DATA FindFileData;
		HANDLE hFind = INVALID_HANDLE_VALUE;
		int index=0;
		struct TAILQ_FileInfo *newp = NULL;

		GetCurrentDirectory(260,olddir);
		
		GenLog(DEBUG_INFO, "%s[%d].解压路径unzippath %s ！\n", __FILE__, __LINE__, unzippath);
		SetCurrentDirectory(unzippath);

		//构建dev列表
		hFind = FindFirstFile("*.dev" ,&FindFileData);
		if( INVALID_HANDLE_VALUE == hFind )
		{
			//有时候找不到
			GenLog(ERROR_INFO,"%s[%d].文件%sParseZip_INVALID_HANDLE_VALUE == hFind\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
		}
		else
		{
			if((FindFileData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==0)
			{
				memset(tmp, 0x00, 260);
				sprintf(tmp, "%s\\%s", unzippath, FindFileData.cFileName);
				_splitpath(tmp, drive, dir, fname, ext);
				index = atoi(fname);
				newp = TAILQ_FileInfoAlloc();
				if(!newp)
				{
					status= -2;
					goto iError;
				}
				memcpy(newp->filename, tmp, strlen(tmp));//文件路径
				newp->offset = index; //文件名(序号)，从1开始
				TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uDevList); //插入链表
			}

			while(FindNextFile( hFind, &FindFileData ))
			{
				if((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					memset(tmp, 0x00, 260);
					sprintf(tmp,"%s\\%s", unzippath, FindFileData.cFileName);
					_splitpath(tmp, drive, dir, fname, ext);
					index = atoi(fname);
					newp = TAILQ_FileInfoAlloc();//最后没有清理，导致内存泄漏
					if(!newp)
					{	
						status= -3;
						goto iError;
					}
					memcpy(newp->filename, tmp, strlen(tmp));//文件路径
					newp->offset = index; //文件名(序号)
					TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uDevList); //插入链表
				}
			}
		}
		FindClose(hFind);
		
			GenLog(DEBUG_INFO, "%s[%d]bIsBigFile: %d \n", __FILE__, __LINE__,bIsBigFile);
		if (bIsBigFile == TRUE)
		{
		//构建大文件格式列表
			hFind = FindFirstFile("00000001.*" ,&FindFileData );
			if( INVALID_HANDLE_VALUE != hFind )
			{
				m_nFileType = BIG_FILETYPE;    //pdf文件格式
				memset(tmp, 0x00,260);
				sprintf(tmp, "%s\\%s", unzippath, FindFileData.cFileName);
				newp = TAILQ_FileInfoAlloc();
				if(!newp)
				{
					status = -2;
					goto iError;
				}
				memcpy(newp->filename, tmp, strlen(tmp));//文件路径
				newp->offset = 1; //文件名(序号)，从1开始
				TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uPageList); //插入链表
			}

			FindClose(hFind);
			SetCurrentDirectory(olddir);
			GenLog(DEBUG_INFO, "%s[%d].大文件打印文件：%s ParseZip_End_Sucess\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 单双面为: %d \n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.nPrintDouble);
		}
		else
		{
			//构建emf列表
			hFind = FindFirstFile("*.pdf" ,&FindFileData );
			if( INVALID_HANDLE_VALUE != hFind )
			{
				//////  [7/24/2018 Administrator]
				////char cdir[MAX_PATH]={0x00};
				////char cfilename[MAX_PATH]={0x00};
				////char cext[32]={0x00};
				////char cDirTem[MAX_PATH] = {0x00};
				////char cdriv[32]={0x00};
				////char tem[1024] = {0x00};
				////CString str1,srcPath;
				////srcPath = decryptZipPath;
				////_splitpath(decryptZipPath,cdriv,cdir,cfilename,cext);	
				////str1 = srcPath.Left(srcPath.ReverseFind('\\'));
				////GenLog(ERROR_INFO,"%s[%d].PDF转BMP输出路径为： %s\n", __FILE__, __LINE__, str1.GetBuffer());
				////if (!(PDFTOBMP(decryptZipPath,str1.GetBuffer(),1)))
				////{
				////	GenLog(DEBUG_INFO,"%s[%d]PDFTOBMP 异常退出\n",__FILE__,__LINE__);
				////}
				//////PS2PDF(decryptZipPath,str1.GetBuffer(),1);
				////DeleteFile(decryptZipPath);
				////FindClose(hFind);
				////hFind = FindFirstFile("*.bmp" ,&FindFileData );
				////if(INVALID_HANDLE_VALUE == hFind )
				////{
				////	FindClose(hFind);
				////	// hFind = FindFirstFile("*.jpg" ,&FindFileData );
				////	hFind = FindFirstFile("*.bmp" ,&FindFileData );
				////	m_nFileType = BMP_FILETYPE;
				////}
				////else
				////{
				////	// emf
				////	m_nFileType = BMP_FILETYPE;
				////}

				//////m_nFileType = EMF_FILETYPE;
				////if((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				////{
				////	memset(tmp, 0x00,260);
				////	sprintf(tmp, "%s\\%s", unzippath,FindFileData.cFileName);
				////	_splitpath(tmp, drive, dir, fname, ext);
				////	index = atoi(fname);
				////	newp = TAILQ_FileInfoAlloc();//最后没有清理，导致内存泄漏
				////	if(!newp)
				////	{
				////		status = -2;
				////		goto iError;
				////	}
				////	memcpy(newp->filename, tmp, strlen(tmp));//文件路径
				////	newp->offset = index; //文件名(序号)，从1开始
				////	TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uPageList); //插入链表
				////}
				////else
				////{
				////	GenLog(ERROR_INFO,"%s[%d].文件%s(ParseZip_FindFileData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
				////}

				////while(FindNextFile( hFind,&FindFileData ))
				////{
				////	if((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				////	{
				////		memset(tmp, 0x00, 260);
				////		sprintf(tmp,"%s\\%s", unzippath, FindFileData.cFileName);
				////		_splitpath(tmp, drive, dir, fname, ext);
				////		index = atoi(fname);
				////		newp = TAILQ_FileInfoAlloc();//最后没有清理，导致内存泄漏
				////		if(!newp)
				////		{	
				////			status= -3;
				////			goto iError;
				////		}
				////		memcpy(newp->filename, tmp, strlen(tmp));//文件路径
				////		newp->offset = index; //文件名(序号)
				////		TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uPageList); //插入链表
				////	}
				////}


				m_nFileType = PDF_FILETYPE;    //pdf文件格式
				
				memset(tmp, 0x00,260);
				sprintf(tmp, "%s\\%s", unzippath,FindFileData.cFileName);
				newp = TAILQ_FileInfoAlloc();
				if(!newp)
				{
					status = -2;
					goto iError;
				}
				memcpy(newp->filename, tmp, strlen(tmp));//文件路径
				newp->offset = 1; //文件名(序号)，从1开始
				TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uPageList); //插入链表
				//}
			}
			else
			{
				FindClose(hFind);
				hFind = FindFirstFile("*.emf" ,&FindFileData );
				if(INVALID_HANDLE_VALUE == hFind )
				{
					FindClose(hFind);
					// hFind = FindFirstFile("*.jpg" ,&FindFileData );
					hFind = FindFirstFile("*.bmp" ,&FindFileData );
					m_nFileType = BMP_FILETYPE;
				}
				else
				{
					// emf
					m_nFileType = EMF_FILETYPE;
				}

				//m_nFileType = EMF_FILETYPE;
				if((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
				{
					memset(tmp, 0x00,260);
					sprintf(tmp, "%s\\%s", unzippath,FindFileData.cFileName);
					_splitpath(tmp, drive, dir, fname, ext);
					index = atoi(fname);
					newp = TAILQ_FileInfoAlloc();//最后没有清理，导致内存泄漏
					if(!newp)
					{
						status = -2;
						goto iError;
					}
					memcpy(newp->filename, tmp, strlen(tmp));//文件路径
					newp->offset = index; //文件名(序号)，从1开始
					TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uPageList); //插入链表
				}
				else
				{
					GenLog(ERROR_INFO,"%s[%d].文件%s(ParseZip_FindFileData.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)!=0\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);
				}

				while(FindNextFile( hFind,&FindFileData ))
				{
					if((FindFileData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
					{
						memset(tmp, 0x00, 260);
						sprintf(tmp,"%s\\%s", unzippath, FindFileData.cFileName);
						_splitpath(tmp, drive, dir, fname, ext);
						index = atoi(fname);
						newp = TAILQ_FileInfoAlloc();//最后没有清理，导致内存泄漏
						if(!newp)
						{	
							status= -3;
							goto iError;
						}
						memcpy(newp->filename, tmp, strlen(tmp));//文件路径
						newp->offset = index; //文件名(序号)
						TAILQ_FileInfodAdd(newp, pCurrInfo->m_JobStatusInfo.m_uPageList); //插入链表
					}
				}
			}
			FindClose(hFind);
			SetCurrentDirectory(olddir);
			GenLog(DEBUG_INFO, "%s[%d].文件%sParseZip_End_Sucess\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode);

		}
	}
	else
	{	
		// 网络模式 [4/14/2015 chenhong]
		if (HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel == WORKING_NETWORK)
		{
			if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
			{
				// 【刷卡器显示】 [9/6/2015 haojia]
				TCHAR cShowMsg[MAX_PATH] = {0x00};
				int nLen = 0;
				strcpy(cShowMsg,_T("文件解压失败..."));			
				nLen = strlen(cShowMsg);
				CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
			}
		}
		GenLog(ERROR_INFO,"%s[%d].文件%s解压失败,原因为%s\n", __FILE__, __LINE__, pCurrInfo->m_PrintJobInfo.szEventCode, GetErrorMessage());
	}
iError:
	return status;
}

//注册表中设置单双面程序
int CHDPrinter::RegSetDuplex(PrintJob* pJobinfo)
{
	int nRet = 1;
	int nDmOrientation = 0;
	int nDmDuplex = 0;
	TCHAR szBatPath[MAX_PATH] = {0x00};

	// 获取横纵向 [9/26/2014 chenhong]
	if (!pJobinfo->m_bIsReceipt)
	{
		HENHMETAFILE hemf ; 
		int papersize = 0;
		struct TAILQ_FileInfo *p;
		p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList);
		hemf = GetEnhMetaFile (p->filename);
		ENHMETAHEADER Emf_head;
		if(GetEnhMetaFileHeader(hemf, sizeof(Emf_head), (LPENHMETAHEADER)&Emf_head))
		{
			// 如果x>y 为横向，否则为纵向 [8/28/2014 chenhong]
			if (Emf_head.szlMillimeters.cx > Emf_head.szlMillimeters.cy)
			{
				nDmOrientation = 2;
			}
			else
			{
				nDmOrientation = 1;
			}
			GenLog(DEBUG_INFO, "%s[%d].第一个EMF 文件横纵向值[%d]！\n",__FILE__,__LINE__, nDmOrientation);
		}		
	}

	// 判断文件单双面和翻页 [9/26/2014 chenhong]
	if(1 == pJobinfo->m_PrintJobInfo.nPrintDouble)
		nDmDuplex = DMDUP_SIMPLEX;
	else if(2 == pJobinfo->m_PrintJobInfo.nPrintDouble)
	{
		if (nDmOrientation == 1)//纵向
		{
			nDmDuplex = DMDUP_VERTICAL;//2左右翻页
		}
		else//横向
		{
			nDmDuplex = 5;
		}
	}
	else
	{
		if (nDmOrientation == 1)
		{
			nDmDuplex = DMDUP_HORIZONTAL;//3上下翻页
		}
		else
		{
			nDmDuplex = 4;
		}
	}

	GenLog(DEBUG_INFO, "%s[%d].文件翻页值[%d]，服务器传来值[%d]！\n",__FILE__,__LINE__, nDmDuplex, pJobinfo->m_PrintJobInfo.nPrintDouble);

	BOOL bRet = TRUE;
	DWORD dwExitCode;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;

	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	sprintf_s(szBatPath, MAX_PATH, _T("RegSetDuplex.bat %d"), nDmDuplex);

	GenLog(ERROR_INFO, _T("%s[%d].[%s]！\n"), __FILE__, __LINE__,szBatPath);
	bRet = CreateProcess(NULL, szBatPath, NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL, 
		&si,
		&pi );
	if( bRet ) //做什么用?
	{// 关闭子进程的主线程句柄  
		//WaitForSingleObject(pi.hProcess, INFINITE);  // 等待子进程的退出       
		GetExitCodeProcess(&pi.hProcess, &dwExitCode); // 获取子进程的退出码
		CloseHandle(pi.hThread);  
		CloseHandle(pi.hProcess); 
	}
	else
	{
		CString strMsg;
		strMsg.Format(_T("设置打印机失败：%s"), GetErrorMessage());
		ShowTipMsg(strMsg.GetBuffer(0), c_btnDelayTime);
		GenLog(ERROR_INFO, _T("%s[%d].[%s]！\n"), __FILE__, __LINE__,strMsg);
	}

	//Sleep(4*1000);
	return nRet;
}

BOOL CHDPrinter::CheckPrinterStretchDibSupport(HDC hdc)
{
	int rasterCaps = 0;
	int supportsStretchDib = 0;
	rasterCaps = GetDeviceCaps(hdc, RASTERCAPS);
	supportsStretchDib = rasterCaps & RC_STRETCHDIB;
	if (supportsStretchDib)
		return TRUE;

	return FALSE;
}

int CHDPrinter::GetFileLengthByName(char *FileName)
{
	int flen;
	FILE *fp;
	fp = fopen(FileName,"rb");
	if(fp==NULL)
		return 0;
	fseek(fp, 0, 2);
	flen = ftell(fp);	
	fseek(fp, 0, 0);
	fclose(fp);
	return flen;
}

BITMAPINFO* CHDPrinter::HDLoadBitmap(const char *pszFilename)
{
	unsigned char *pDib,*m_pDibBits;
	FILE *fp=NULL;
	DWORD dwDibSize;
	BOOL retv=FALSE;
	BITMAPFILEHEADER BFH;
	BITMAPINFOHEADER *m_pBIH=NULL;
	RGBQUAD *m_pPalette;
	int m_nPaletteEntries;
	//打开位图文件

	dwDibSize = GetFileLengthByName((char *)pszFilename)-sizeof(BITMAPFILEHEADER);

	if ((fp = fopen(pszFilename,"rb"))==NULL)
	{
		GenLog(ERROR_INFO,"%s[%d].打开文件失败！ \n",__FILE__,__LINE__);
		return NULL;
	}

	//为DIB位图分配内存
	pDib = (unsigned char *)malloc(dwDibSize);
	if(pDib == NULL)
	{
		GenLog(ERROR_INFO,"%s[%d].为DIB位图分配内存失败！\n",__FILE__,__LINE__);
		retv =true;
		goto out;
	}
	//读取位图文件数据
	if(fread(&BFH,1,sizeof(BITMAPFILEHEADER),fp) != sizeof(BITMAPFILEHEADER))
	{
		GenLog(ERROR_INFO,"%s[%d].该文件不是一个有效的位图\n",__FILE__,__LINE__);
		retv =true;
		goto out;
	}

	if(BFH.bfType !=0x4d42)
	{
		GenLog(ERROR_INFO,"%s[%d].该文件不是一个有效的位图 BFH.bfType=0x%x\n",__FILE__,__LINE__,BFH.bfType);
		retv =true;
		goto out;
	}
	if(fread(pDib,1,dwDibSize,fp) != dwDibSize)
	{
		GenLog(ERROR_INFO,"%s[%d].该文件不是一个有效的位图\n",__FILE__,__LINE__);
		retv =true;
		goto out;

	}

	m_pBIH = (BITMAPINFOHEADER*)pDib;
	m_pPalette=(RGBQUAD*)(pDib+sizeof(BITMAPINFOHEADER));

	//计算调色板中实际颜色数量
	m_nPaletteEntries = 1 << m_pBIH->biBitCount;
	if (m_pBIH->biBitCount > 8)
		m_nPaletteEntries = 0;
	else if (m_pBIH->biClrUsed != 0)
		m_nPaletteEntries = m_pBIH->biClrUsed;
	m_pDibBits = pDib+sizeof(BITMAPINFOHEADER) + m_nPaletteEntries * sizeof (RGBQUAD);

out:
	fclose(fp);
	if(retv){
		if(pDib)
			free(pDib);
		pDib=NULL;
		return NULL;
	}
	return (BITMAPINFO *)m_pBIH;
}

BOOL CHDPrinter::GenerateBarcode2(PrintJob* pJob, int nPageNum, TCHAR* szBarcode2)
{
	//通过当前线程id和printjobid组成Docinfo的docname
	GenLog(DEBUG_INFO,"%s[%d].开始生成二维码数据\n",__FILE__,__LINE__);
	if (szBarcode2 == NULL)
	{
		return FALSE;
	}

	int nFlowCode = 0;
	if (strlen(pJob->m_szFileBarcode) == 0)
	{
		nFlowCode = GenerateBarcode(pJob, TRUE);
		if (nFlowCode == 0)
		{
			return FALSE;
		}
	}
	else
	{
		TCHAR szFlow[7] = {0x00};
		memcpy(szFlow, pJob->m_szFileBarcode + 9, 13);
		nFlowCode = atoi(szFlow);
	}

	/*           基础字段          */
	//加密标志2
	strcat(szBarcode2, "00");
	//二维码版本2
	strcat(szBarcode2, "10");
	//预留3
	strcat(szBarcode2, "000");
	//单位代码或名称4
	strcat(szBarcode2, m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0));
	//员工姓名8
	TCHAR szUserName[9] = {0x00};
	if (strlen(pJob->m_PrintJobInfo.szUserName) > 8)
	{
		memcpy(szUserName, pJob->m_PrintJobInfo.szUserName, 8);
	}
	else
	{
		//不足的补零
		FillZero(szUserName, 8 - strlen(pJob->m_PrintJobInfo.szUserName));
		strcat(szUserName, pJob->m_PrintJobInfo.szUserName);
	}
	strcat(szBarcode2, szUserName);
	//预留8
	strcat(szBarcode2, "00000000");
	//日期8
	time_t nowtime = time(NULL);
	tm *pNowtime=NULL;
	pNowtime = localtime(&nowtime);
	CString strYear;
	strYear.Format(_T("%04d%02d%02d"), pNowtime->tm_year + 1900, pNowtime->tm_mon + 1, pNowtime->tm_mday);
	strcat(szBarcode2, strYear.GetBuffer(0));
	//预留2
	strcat(szBarcode2, "00");
	//密级1
	char szFileType[c_nChar32] = {0x00};
	sprintf_s(szFileType, c_nChar32, "%d", pJob->m_PrintJobInfo.nSeclvCode);
	strcat(szBarcode2, szFileType);
	//预留2
	strcat(szBarcode2, "00");

	//////////////////////////////////////////////////////////////////////////
	// 拓展字段
	//预留7
	strcat(szBarcode2, "0000000");
	//介质1
	strcat(szBarcode2, "Z");
	//来源1
	if (pJob->m_PrintJobInfo.nPrintType == 1)
	{
		strcat(szBarcode2, "D");
	}
	else if (pJob->m_PrintJobInfo.nPrintType == 2)
	{
		strcat(szBarcode2, "T");
	}

	//页数4
	TCHAR szPages[4] = {0x00};
	sprintf(szPages, "%04d", pJob->m_PrintJobInfo.nPageCount);
	strcat(szBarcode2, szPages);
	//页码4
	TCHAR szPageNum[4] = {0x00};
	sprintf(szPages, "%04d", nPageNum + 1);
	strcat(szBarcode2, szPageNum);
	//预留5
	strcat(szBarcode2, "00000");
	//流水号13
	TCHAR szFlowCode[13] = {0x00};
	sprintf(szFlowCode, "%013d", nFlowCode);
	strcat(szBarcode2, szFlowCode);
	//文件名称128
	TCHAR szFileName[c_nChar128] = {0x00};
	if (strlen(pJob->m_PrintJobInfo.szFileName) > c_nChar128)
	{
		memcpy(szFileName, pJob->m_PrintJobInfo.szFileName, c_nChar128);
	}
	else
	{
		//不足的补零
		FillZero(szFileName, c_nChar128 - strlen(pJob->m_PrintJobInfo.szFileName) - 1);
		strcat(szFileName, pJob->m_PrintJobInfo.szFileName);
	}
	strcat(szBarcode2, szFileName);
	//制作部门4
	TCHAR szGroupID[5] = {0x00};	
	if (strlen(pJob->m_PrintJobInfo.szGroupID) > 4)
	{
		memcpy(szGroupID, pJob->m_PrintJobInfo.szGroupID, 4);
	}
	else
	{
		FillZero(szGroupID, 4 - strlen(pJob->m_PrintJobInfo.szGroupID));
		strcat(szGroupID, pJob->m_PrintJobInfo.szGroupID);
	}
	strcat(szBarcode2, szGroupID);
	//原始编码22
	strcat(szBarcode2, pJob->m_szFileBarcode);

	GenLog(DEBUG_INFO,"%s[%d].生成二维码数据ok\n",__FILE__,__LINE__);
	return TRUE;
}

//增加参数 m_PrintJobInfo_nPrintType 判断针式打印 by zkx 20240612

int CHDPrinter::GetPaperSize(int m_PrintJobInfo_nPrintType,TAILQ_FileInfo* fileinfo, short* orientation,HDC *hdc,int pageNo_PDF)
{
	GenLog(DEBUG_INFO, _T("%s[%d].开始获取纸张类型！\n"), __FILE__, __LINE__);
	int papersize = 0;
	BOOL bFlag = FALSE;
	geomutil::SizeT<float> pSize;
	HENHMETAFILE hemf ; 
	ENHMETAHEADER Emf_head;
	BITMAP bmpinfo;
	HBITMAP maskBMP = NULL;

	if(m_nFileType==EMF_FILETYPE)
	{
		hemf = GetEnhMetaFile (fileinfo->filename);
		bFlag = GetEnhMetaFileHeader(hemf, sizeof(Emf_head), (LPENHMETAHEADER)&Emf_head);//获取emf文件头，用于获取文件宽高
	}
	else if(m_nFileType==PDF_FILETYPE)
	{
		bFlag = TRUE;
	}
	else if (m_nFileType == BMP_FILETYPE)
	{  
		maskBMP = (HBITMAP)LoadImage(NULL, fileinfo->filename, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION | LR_DEFAULTSIZE);     
		GetObject(maskBMP, sizeof(bmpinfo), &bmpinfo);
		bFlag = TRUE;
	}
	GenLog(DEBUG_INFO, _T("%s[%d].emf文件路径%s！\n"), __FILE__, __LINE__,fileinfo->filename);
	if(bFlag)
	{
		/////////////////////////////////////////////获取纸张编号////////////////////////////////////////////////////////////
		//int wordsize=sizeof(WORD);
		TCHAR devstring[256] = {0x00};
		char* driver = NULL;
		char* printerport= NULL;
		GetProfileString("Devices", m_strPrinterPath, "", devstring, sizeof(devstring));
		driver = strtok ((char*)devstring, (const char *)",");
		printerport = strtok(NULL, (const char *) ",");

		GenLog(ERROR_INFO, "%s[%d].driver=%s;printerport！\n", __FILE__, __LINE__,driver,printerport);
		int nos = DeviceCapabilities(
			m_strPrinterPath,         // printer name
			printerport,           // port name
			DC_PAPERS,       // device capability
			NULL,          // output buffer
			NULL//CONST DEVMODE *pDevMode  // device data buffer
			);

		if (nos <= 0)
		{
			//MessageBox(NULL,"请确认打印机的驱动是否正常。\n", "航盾打印控制台", c_uintButtonStyle);
			GenLog(ERROR_INFO, "%s[%d].提取打印机纸张失败！\n", __FILE__, __LINE__);
			GenLog(ERROR_INFO, "%s[%d].提取打印机纸张失败=%d！\n", __FILE__, __LINE__,GetLastError());
			return -1;
		}
		LPWORD pOutput = new WORD[nos];
		memset(pOutput, 0x00, nos*sizeof(WORD));
		nos = DeviceCapabilities(
			m_strPrinterPath,         // printer name
			printerport,
			DC_PAPERS,       // device capability
			(LPSTR)pOutput,          // output buffer
			NULL//CONST DEVMODE *pDevMode  // device data buffer
			);

		PAPERINFO *pPaperINFO = new PAPERINFO[nos];
		memset(pPaperINFO, 0x00, nos*sizeof(PAPERINFO));

		for (int i=0; i < nos ;i++)
		{
			if (IsWow64())
			{
				pPaperINFO[i].PAPERNUM = pOutput[i]&0xffff;// 警告：只适合用于32位机，如果为64位，应为0xffff；
			}
			else
			{
				// 修改特殊打印机papersize超过两位字节的计算方案 [8/28/2014 chenhong]
				//pPaperINFO[i].PAPERNUM = pOutput[i]&0xff;// 警告：只适合用于32位机，如果为64位，应为0xffff；
				pPaperINFO[i].PAPERNUM = pOutput[i]&0xffff;
				// End [8/28/2014 chenhong]				
			}
		}

		if (pOutput)
		{
			delete(pOutput);
			pOutput = NULL;
		}

		/////////////////////////////////////////////////获取纸张大小///////////////////////////////////////////////////////////////
		//int pointsize=sizeof(POINT);

		nos = DeviceCapabilities(
			m_strPrinterPath,         // printer name
			printerport,           // port name
			DC_PAPERSIZE,       // device capability
			NULL,          // output buffer
			NULL//CONST DEVMODE *pDevMode  // device data buffer
			);

		if (nos <= 0)
		{
			//MessageBox(NULL,"请确认打印机的驱动是否正常。\n", "航盾打印控制台", c_uintButtonStyle);
			GenLog(ERROR_INFO, "%s[%d].提取打印机纸张失败！\n",__FILE__,__LINE__);
			return -1;
		}

		GenLog(DEBUG_INFO, "%s[%d].打印机[%s]纸张类型数量[%d]！\n",__FILE__,__LINE__, m_strPrinterPath, nos);
		LPPOINT pSizeOutput = new POINT[nos];
		memset(pSizeOutput, 0x00, nos*sizeof(POINT));

		nos = DeviceCapabilities(
			m_strPrinterPath,         // printer name
			printerport,
			DC_PAPERSIZE,       // device capability
			(LPSTR)pSizeOutput,          // output buffer
			NULL//CONST DEVMODE *pDevMode  // device data buffer
			);		
		if (nos <= 0)
		{
			//MessageBox(NULL,"请确认打印机的驱动是否正常。\n", "航盾打印控制台", c_uintButtonStyle);
			GenLog(ERROR_INFO, "%s[%d].提取打印机纸张失败！\n",__FILE__,__LINE__);
			return -1;
		}
		/////////////////////////////////////////////////匹配纸张///////////////////////////////////////////////////////////////////

		GenLog(DEBUG_INFO, "%s[%d].打印机纸张类型[%ld,%ld]！\n",__FILE__,__LINE__, pSizeOutput->x,pSizeOutput->y);
		int tmpWidth = 0;
		int tmpHeight = 0;
		if(m_nFileType == EMF_FILETYPE)
		{
			if (Emf_head.szlMillimeters.cx > Emf_head.szlMillimeters.cy)
			{
				tmpWidth = Emf_head.szlMillimeters.cy;
				tmpHeight = Emf_head.szlMillimeters.cx;
			}
			else
			{
				tmpWidth = Emf_head.szlMillimeters.cx;
				tmpHeight = Emf_head.szlMillimeters.cy;
			}
			GenLog(DEBUG_INFO, "%s[%d].EMF 宽和高分别为%d %d！\n",__FILE__,__LINE__, tmpWidth, tmpHeight);

		}
		else if(m_nFileType == PDF_FILETYPE)
		{
			pSize = m_pEngine->PageMediabox(pageNo_PDF).Size().Convert<float>();
			//RectD rect = m_pEngine->PageContentBox(pageNo_PDF, Target_Print);
			const float dpiFactor = (m_pEngine->GetFileDPI()/25.39999918);
			if (pSize.dx > pSize.dy)  //核实单元尺寸
			{
				tmpWidth = pSize.dy/dpiFactor;
				tmpHeight = pSize.dx/dpiFactor;
			}
			else
			{
				tmpWidth = pSize.dx/dpiFactor;
				tmpHeight = pSize.dy/dpiFactor;
			}
			GenLog(DEBUG_INFO, "%s[%d].PDF 宽和高分别为%d %d！\n",__FILE__,__LINE__, tmpWidth, tmpHeight);
		}
		else if (m_nFileType == BMP_FILETYPE)
		{
			// 驱动默认DPI300
			if (bmpinfo.bmWidth > bmpinfo.bmHeight)
			{
				tmpWidth = bmpinfo.bmHeight*25.4/300;
				tmpHeight = bmpinfo.bmWidth*25.4/300;
			}
			else
			{
				tmpWidth = bmpinfo.bmWidth*25.4/300;
				tmpHeight = bmpinfo.bmHeight*25.4/300;
			}
			GenLog(DEBUG_INFO, "%s[%d].BMP 宽和高分别为%d %d！\n",__FILE__,__LINE__, tmpWidth, tmpHeight);
		}


		for (int i = 0; i < nos; i++)
		{
			//POINT lptmp;
			//memcpy(&lptmp,&pSizeOutput[i],sizeof(POINT));

			pPaperINFO[i].PAPERSIZE_WidthMilimeters = pSizeOutput[i].x;
			pPaperINFO[i].PAPERSIZE_HeightMilimeters = pSizeOutput[i].y;

			int WidthMilimeters = pPaperINFO[i].PAPERSIZE_WidthMilimeters/10;	//lptmp.x,lptmp.y获取的结果是以毫米为单位，小数点后两位，例如：2100
			int HeightMilimeters = pPaperINFO[i].PAPERSIZE_HeightMilimeters/10;

			GenLog(DEBUG_INFO, "%s[%d]匹配中，打印机纸张%d类型宽和高分别为%d %d！\n",__FILE__, __LINE__, i,WidthMilimeters, HeightMilimeters); 
			if ((tmpWidth >= WidthMilimeters-2 && tmpWidth <= WidthMilimeters+2) && (tmpHeight >= HeightMilimeters-2 && tmpHeight <= HeightMilimeters+2))
			{
				papersize = pPaperINFO[i].PAPERNUM;		//  i/8-1为之前获取的第几个纸张号
				GenLog(DEBUG_INFO, "%s[%d]匹配成功，纸张宽和高分别为%d %d！\n",__FILE__, __LINE__, WidthMilimeters, HeightMilimeters); 
				if(m_nFileType==EMF_FILETYPE)
				{
					if (Emf_head.szlMillimeters.cx > Emf_head.szlMillimeters.cy)		//如果x>y 为横向，否则为纵向
					{
						*orientation = 2;
					}
					else
					{
						*orientation = 1;
					}
				}
				else if(m_nFileType==PDF_FILETYPE)
				{
					if (pSize.dx > pSize.dy)		//如果x>y 为横向，否则为纵向
					{
						*orientation = 2;
					}
					else
					{
						*orientation = 1;
					}
				}
				else if (m_nFileType == BMP_FILETYPE)
				{
					if (bmpinfo.bmWidth > bmpinfo.bmHeight)
					{
						*orientation = 2;
					}
					else
					{
						*orientation = 1;
					}
				}

				if (pSizeOutput)
				{
					delete(pSizeOutput);
					pSizeOutput = NULL;
				}

				if (pPaperINFO)
				{
					delete(pPaperINFO);
					pPaperINFO = NULL;
				}
				if(m_nFileType==EMF_FILETYPE)
				{
					DeleteEnhMetaFile (hemf) ;
				}
				if (m_nFileType == BMP_FILETYPE)
				{
					DeleteObject(maskBMP);
				}

				hemf = NULL;
				fileinfo=NULL;

				GenLog(DEBUG_INFO, "%s[%d]匹配到的纸张编号为%d！\n",__FILE__,__LINE__, papersize);	
				return papersize;
			}
		}

		if (pSizeOutput)
		{
			delete(pSizeOutput);
			pSizeOutput = NULL;
		}

		if (pPaperINFO)
		{
			delete(pPaperINFO);
			pPaperINFO = NULL;
		}
		GenLog(DEBUG_INFO, "%s[%d]纸张匹配失败！\n",__FILE__, __LINE__);
		//找不到，增加自定义纸张
		if (!papersize)
		{
			PAPERNAME szPaperName;
			sprintf(szPaperName, "HD%dx%d", tmpWidth, tmpHeight);
			if (AddCustomPaper(szPaperName, CSize(tmpWidth*10, tmpHeight*10), CRect(0,0,0,0)))
			{
				PRINTER_INFO_2 *ppi2 = GetInfo2();
				if (ppi2)
				{
					papersize = GetPaperSize(ppi2->pPortName, szPaperName);
					GlobalFree((HGLOBAL)ppi2);
				}
				else
				{
					GenLog(ERROR_INFO, "%s[%d].分配PRINTER_INFO_2空间失败！\n",__FILE__, __LINE__);
				}
			}
			else
			{
				GenLog(ERROR_INFO, "%s[%d].增加默认纸张%s失败！\n",__FILE__, __LINE__, szPaperName);
			}
		}

		if(m_nFileType==EMF_FILETYPE)
		{
			DeleteEnhMetaFile (hemf) ;
		}
		hemf = NULL;
		fileinfo=NULL;
		*orientation = 0;
	}

	return papersize;
}


int CHDPrinter::GetPaperSizeByDriver(PrintJob* pJobinfo, LPDEVMODE &devMode, HDC* hdcPrint)
{
	GenLog(DEBUG_INFO,"%s[%d].start GetPaperSizeByDriver\n",__FILE__, __LINE__);
	int nPaperNum = 0;

	HANDLE hPrinter = NULL;
	DWORD structSize = 0;
	DWORD returnCode = 0;

	PRINTER_DEFAULTS pDefault;
	pDefault.DesiredAccess = PRINTER_ALL_ACCESS;
	pDefault.pDatatype = NULL;
	pDefault.pDevMode = NULL;
	if (!OpenPrinter(m_strPrinterPath.GetBuffer(0), &hPrinter, &pDefault)) 
	{
		GenLog(ERROR_INFO,"%s[%d].文件%s打开打印机失败\n",__FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode);
		return 0;
	}

	//调用两次DocumentProperties是为了先把驱动里的设置读取出来，然后在去打开驱动设置窗口，防止将设置丢掉
	returnCode = DocumentProperties(NULL,
		hPrinter,
		(LPSTR) m_strPrinterPath.GetBuffer(0),
		devMode,					/* The address of the buffer to fill. */ 
		NULL,						/* Not using the input buffer. */ 
		/*DM_IN_PROMPT |*/DM_OUT_BUFFER);				/* Have the output buffer filled. */ 

	returnCode = DocumentProperties(NULL,
		hPrinter,
		(LPSTR) m_strPrinterPath.GetBuffer(0),
		devMode,					/* The address of the buffer to fill. */ 
		NULL,						/* Not using the input buffer. */ 
		DM_IN_PROMPT | DM_OUT_BUFFER);				/* Have the output buffer filled. */ 

	if (returnCode != IDOK) 
	{
		GenLog(ERROR_INFO,"%s[%d].文件%s高级打印选项中获取打印机属性失败。\n",__FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode);
		ClosePrinter(hPrinter);
		return 0;
	}

	//Sleep(250);

	if (!ClosePrinter(hPrinter))
	{
		GenLog(ERROR_INFO,"%s[%d].文件%s关闭打印机失败。\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode);
	}

	return devMode->dmPaperSize;
}

//add by zbin 20190523
//EMF文件添加暗水印接口
/*
*     0成功
*     1 所有参数都不能传NULL
*     2所有参数都不能传空
*     3 水印信息的长度错误
*     4 路径不存在
*     6 EMF文件解析失败 (此错误属于单个文件的错误，此时会在同级目录下生成failFilelist.log日志，记录失败的文件，通常发生此错误肯定是这个EMF文件本身有问题)
*     7 打印机设备获取失败
*     8 SofosofiWatermark.dll加载失败
*     9 watermarkEMF.dll加载失败
*     10 存在参数为空的情况
*     11 未知错误
*    ps:除错误代码6之外的所有错误都属于整体错误，并不会生成failFilelist.log日志
*/
int CHDPrinter::AddWaterMark2EMF(CString emfPath,CString decemfPath,CString wmCode,CString keyCode,CString printerPath,int nBytes,bool bCompany)
{
	int nRet = 11;//other info
	if (emfPath.IsEmpty() ||  wmCode.IsEmpty() || printerPath.IsEmpty())
	{
		//如果参数为空
		GenLog(ERROR_INFO,"%s[%d].CHDPrinter::AddWaterMark2EMF::emfPath:%s,wmCode:%s,printerName:%s\n",__FILE__, __LINE__, emfPath,wmCode,printerPath);
		nRet = 10;
	} 
	else
	{
		//插入水印信息“
		HINSTANCE hIns = ::LoadLibrary(_T("watermarkEMF.dll"));
		if (hIns != NULL)
		{
			pEMFWatermarkEmbed m_fnImageEmbed = (pEMFWatermarkEmbed)::GetProcAddress(hIns,"EMFWatermarkEmbed");
			if (m_fnImageEmbed != NULL)
			{
				int temp = wmCode.GetLength();
				nRet = m_fnImageEmbed(emfPath.GetBuffer(0),decemfPath.GetBuffer(0), wmCode.GetBuffer(0),NULL, printerPath.GetBuffer(0),2,false);
			}

			::FreeLibrary(hIns);
		}
	}

	//根据结果码返回不同信息
	switch(nRet)
	{
	case 0:
	case 1:
		{
			GenLog(DEBUG_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF() 生成暗水印成功！\n",__FILE__, __LINE__);
			return 0;
		}
	
	case 2:
	case 10:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF() 所有参数都不能传NULL或者空！\n",__FILE__, __LINE__);
			return -1;
		}
	case 3:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF()  暗水印:%s水印长度必须32位！\n",__FILE__, __LINE__,wmCode);
			return -1;
		}
	case 4:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF()  EMF文件路径:%s路径不存在\n",__FILE__, __LINE__,emfPath);
			return -1;
		}
	case 5:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF()  EMF文件:%s解析失败\n",__FILE__, __LINE__,emfPath);
			return -1;
		}
	case 7:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF()  打印机设备:%s获取失败\n",__FILE__, __LINE__,printerPath);
			return -1;
		}
	case 8:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF() SofosofiWatermark.dll加载失败\n",__FILE__, __LINE__);
			return -1;
		}
	case 9:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF() watermarkEMF.dll加载失败\n",__FILE__, __LINE__);
			return -1;		
		}
	case 11:
	default:
		{
			GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF() 加载水印库失败\n",__FILE__, __LINE__);
			return -1;
		}
	}
}

int CHDPrinter::PrintOneDoc(PrintJob* pJobinfo,LPDEVMODE devMode, HDC* hdcPrint, int index,int * Epage)
{
	//注释里要体现文件编号和第几份
	int status = 0;
	int i = 0;
	int iJobid = 0;
	int nPageCount=1;
	//判断条码获取方式
	GenLog(ERROR_INFO, "%s[%d].判断条码获取方式%d\n", __FILE__, __LINE__,m_HDAppConfig->m_ExConfig.m_nCreateBarcode);
	if (m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER)
	{
		GenLog(ERROR_INFO, "%s[%d].从服务器获取条码值\n", __FILE__, __LINE__);
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=pJobinfo->m_PrintJobInfo.nBarcodeType;
		pApplyBarcode.nEventType = 1;//打印1 ，刻录2
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));

		//首先根据流水号生成条码
		GenLog(ERROR_INFO, "%s[%d].UERID[%s],作业编号[%s],条码类型[%d]，集团标识[%d],控制台号[%s]\n", \
			__FILE__, __LINE__,pApplyBarcode.cUserID,pApplyBarcode.cEventCode,pApplyBarcode.nBarcodeType,pApplyBarcode.nCompanyType,pApplyBarcode.cConsoleID);

		if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
		{
			if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
			{
				if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
				{
					// 【刷卡器显示】 [9/6/2015 haojia]
					TCHAR cShowMsg[MAX_PATH] = {0x00};
					int nLen = 0;
					strcpy(cShowMsg,_T("申请条码失败"));			
					nLen = strlen(cShowMsg);
					CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
				}
			}	
			CString str;
			str.Format("打印任务[%s]申请条码失败,稍候请重新打印！", pJobinfo->m_PrintJobInfo.szFileName);
			ShowMsgBox(str.GetBuffer(0), MB_OK);
			GenLog(ERROR_INFO, "%s[%d].打印任务[%s]申请条码失败！ \n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
			return -1;
		}

		// 获取条码值 [1/8/2015 chenhong]
		sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));

		memset(m_szBarcode2Code, 0x00, sizeof(m_szBarcode2Code));
		sprintf_s(m_szBarcode2Code, 1024, _T("%s"), m_piocp->GetBarcodeValue(PDF417_CODE));

	}
	else if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_HOST)//默认本地生成条码
	{
		GenLog(ERROR_INFO, "%s[%d].本地生成条码值\n", __FILE__, __LINE__);
		if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
		{
			if (GenerateCETCBarcode(pJobinfo) == 0)
			{
				GenLog(ERROR_INFO, "%s[%d].打印任务%s申请大流水号失败！\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
				return -1;
			}
		}
		else
		{
			// 网络模式 [10/15/2014 chenhong]
			if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK || strcmp(m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0),"CAEP") == 0)
			{
				if (GenerateCAEPBarcode(pJobinfo) == 0)
				{
					GenLog(ERROR_INFO, "%s[%d].打印任务%s申请大流水号失败！\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
					return -1;
				}
			}
			else
			{
				if (GenerateBarcode(pJobinfo) == 0)
				{
					GenLog(ERROR_INFO, "%s[%d].打印任务%s申请大流水号失败！\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
					return -1;
				}
			}
		}
	}
	else
	{
		GenLog(ERROR_INFO, "%s[%d].index：%d\n", __FILE__, __LINE__,index);
		if(index==0)
		{
			GenLog(ERROR_INFO, "%s[%d].从服务器获取条码值\n", __FILE__, __LINE__);
			APPLY_BARCODE pApplyBarcode;
			strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
			strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
			pApplyBarcode.nBarcodeType=pJobinfo->m_PrintJobInfo.nBarcodeType;
			pApplyBarcode.nEventType = 1;//打印1 ，刻录2
			pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
			strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
			//首先根据流水号生成条码
			GenLog(ERROR_INFO, "%s[%d].UERID[%s],作业编号[%s],条码类型[%d]，集团标识[%d],控制台号[%s]\n", \
				__FILE__, __LINE__,pApplyBarcode.cUserID,pApplyBarcode.cEventCode,pApplyBarcode.nBarcodeType,pApplyBarcode.nCompanyType,pApplyBarcode.cConsoleID);

			if(!CDistributeThread::Instance()->ApplyBarcode_Batch(&pApplyBarcode,pJobinfo->m_PrintJobInfo.nPrintCount))
			{
				CString str;
				str.Format("打印任务[%s]申请条码失败,稍候请重新打印！", pJobinfo->m_PrintJobInfo.szFileName);
				ShowMsgBox(str.GetBuffer(0), MB_OK);
				GenLog(ERROR_INFO, "%s[%d].打印任务[%s]申请条码失败！ \n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
				return -1;
			}

			// 获取条码值 [1/8/2015 chenhong]
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));

			memset(m_szBarcode2Code, 0x00, sizeof(m_szBarcode2Code));
			sprintf_s(m_szBarcode2Code, 1024, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));
		}
		else
		{
			GenLog(ERROR_INFO, "%s[%d].index：%d\n", __FILE__, __LINE__,index);
			int len =strlen(m_szBarcode2Code);
			char numPart[15]={0};
			strncpy(numPart,m_szBarcode2Code+len-13,13);
			long long num = _atoi64(numPart)+1;
			sprintf(m_szBarcode2Code+len-13,"%013lld",num);
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_szBarcode2Code);
		}
	}
	//记录台账
	//PrintResult printResult;
	//strcpy(printResult.m_szFileBarcode, pJobinfo->m_szFileBarcode);
	//strcpy(printResult.m_szFileNo, pJobinfo->m_PrintJobInfo.szEventCode);
	//JOB_INFO_2  currentJob = {0x00};
	//GetPrintResult(pJobinfo,currentJob , 2, &printResult);
	//CResultThread::Instance()->InsertResultList(&printResult,pJobinfo->m_PrintJobInfo.nReceipt);
	// 记录条码类型 [4/1/2015 chenhong]
	m_nBarcodeType = pJobinfo->m_PrintJobInfo.nBarcodeType;
	// 判断是否为LINUX系统 [11/14/2018 Administrator]
	int nFlags = HDAppConfig::Instance()->m_ExConfig.m_IsLinux;
	GenLog(ERROR_INFO, "%s[%d]. 客户端操作系统为：nFlags = %d ，1为LINUX，0为WINDOWS！\n", __FILE__, __LINE__, nFlags);

	char szDocNameBuf[MAX_PATH*2] = {0x00};
	DOCINFO di = { sizeof (DOCINFO), NULL };
	sprintf(szDocNameBuf,"HDPrint:Console-%s#Document-%s$", m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0), pJobinfo->m_szFileBarcode);
	di.lpszDocName = szDocNameBuf;

	GenLog(ERROR_INFO, "%s[%d]. szDocNameBuf=%s！\n", __FILE__, __LINE__, szDocNameBuf);
	struct TAILQ_FileInfo *p,*next;
	GenLog(ERROR_INFO, "%s[%d].m_nFileType！%d\n", __FILE__, __LINE__,m_nFileType);
	TCHAR szDefPrinter[1024] = {0};
	DWORD length = 1024;

	if (m_nFileType == BIG_FILETYPE)
	{
		GetDefaultPrinter(szDefPrinter, &length);
		SetDefaultPrinter(m_strPrinterPath.GetBuffer());
	}
	else if ((m_nFileType == PDF_FILETYPE) && (nFlags != 0))
	{
		WinExec(_T("taskkill -f -im SetPrinter.exe -im syssrv.exe"),SW_HIDE);
		Sleep(500);	
		GetDefaultPrinter(szDefPrinter, &length);
		SetDefaultPrinter(m_strPrinterPath.GetBuffer());
	}
	else
	{
		GenLog(ERROR_INFO, "%s[%d].start doc！\n", __FILE__, __LINE__);

		iJobid = StartDoc(*hdcPrint, &di);
		GenLog(ERROR_INFO, "%s[%d].start doc:%s,renturn :%d！\n", __FILE__, __LINE__,di.lpszDocName,iJobid);

		if (iJobid <= 0)
		{
			int err = GetLastError();
			GenLog(ERROR_INFO, "%s[%d].初始化打印文档错误,错误编码:[%d]，%s", __FILE__, __LINE__,err, GetErrorMessage());
			return -1;
		}

	}

	if (!TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList))
	{
		GenLog(ERROR_INFO,"%s[%d].TAILQ_FIRST错误，发送解锁包！\n",__FILE__, __LINE__);
		status = -1;
		goto Exit;
	}

	/*****************************高级打印中选择补打的起始页和结束页****************************/
	if (pJobinfo->m_JobStatusInfo.m_nStartPage != 0)
	{
		if (pJobinfo->m_JobStatusInfo.m_nStartPage > pJobinfo->m_PrintJobInfo.nPageCount)
		{
			//起始页超过了总页数，报错
			return -1;
		}
		else
		{
		}

		GenLog(ERROR_INFO,"%s[%d].pJobinfo->m_JobStatusInfo.m_nStartPage =%d！\n",__FILE__, __LINE__,pJobinfo->m_JobStatusInfo.m_nStartPage);
	}
	else
	{
		pJobinfo->m_JobStatusInfo.m_nStartPage = 1;	//这步主要是用于AttachBarcode函数
		GenLog(ERROR_INFO,"%s[%d].pJobinfo->m_JobStatusInfo.m_nStartPage=1！\n",__FILE__, __LINE__);
	}

	if (pJobinfo->m_JobStatusInfo.m_nEndPage != 0)
	{
		if (pJobinfo->m_JobStatusInfo.m_nEndPage > pJobinfo->m_PrintJobInfo.nPageCount)
		{
			//结束页大于页数了，也不对
			return -1;
		}
		else
		{
		}

		GenLog(ERROR_INFO,"%s[%d].pJobinfo->m_JobStatusInfo.m_nEndPage =%d！\n",__FILE__, __LINE__,pJobinfo->m_JobStatusInfo.m_nEndPage);
	}
	else
	{
		pJobinfo->m_JobStatusInfo.m_nEndPage = pJobinfo->m_PrintJobInfo.nPageCount;		//这步主要是用于AttachBarcode函数

		GenLog(ERROR_INFO,"%s[%d].pJobinfo->m_JobStatusInfo.m_nEndPage =%d！\n",__FILE__, __LINE__,pJobinfo->m_JobStatusInfo.m_nEndPage);
	}
	//////////////////////////////////////////////////////////////////////////



	if(m_nFileType == EMF_FILETYPE || m_nFileType == BMP_FILETYPE)
	{
		GenLog(ERROR_INFO,"%s[%d].m_nFileType =%d！\n",__FILE__, __LINE__,m_nFileType);
		//获取emf文件路径
		CString _fileName;
		for (p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList); p; p = next)
		{
			GenLog(ERROR_INFO,"%s[%d].PrintOneDoc函数中循环打印第%d份，p->filename为 ：%s !\n", __FILE__, __LINE__,index,p->filename);
			next = TAILQ_NEXT(p, chain);
			_fileName = p->filename;

			if(pJobinfo->m_JobStatusInfo.m_nStartPage > p->offset)
				continue;

			if(pJobinfo->m_JobStatusInfo.m_nEndPage < p->offset)
				break;

			//
			//首先判断是否添加暗水印
			//add by zbin 20190523
			//针对EMF文件增加暗水印信息
			//首先先判断是否添加水印信息
			if(1 == HDAppConfig::Instance()->m_ExConfig.m_nIsWaterMark)
			{
				//EMF文件添加暗水印信息
				//暗水印必须暂时32位01串
				CString strWaterMark = m_piocp->GetWaterMark();
				if(0 >= strWaterMark.GetLength())
				{
					GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::GetWaterMark()  获取暗水印信息失败！\n",__FILE__, __LINE__);
					status = -1;
					goto Exit;
				}
				else
				{
					//
					//转码
					CString strFinalBarCode;
					for (int i = 0 ; i < strWaterMark.GetLength();i++)
					{
						//
						CString _str(strWaterMark.GetAt(i));
						bitset<4> bitset1(_ttoi(_str.GetBuffer(0)));
						std::string strTemp = bitset1.to_string();
						strFinalBarCode.Append(strTemp.c_str());
					}
					//
					/*strFinalBarCode.Append("0000");*/
					//
					char _path[MAX_PATH] = {0x00}; 
					char drive[3] = {0x00}; 
					char emfname[260]={0x00};     
					char ext[256]={0x00}; 
					_splitpath(_fileName.GetBuffer(0),drive,_path,emfname,ext);
					int _length_file = _fileName.GetLength();
					int offset = _fileName.ReverseFind('\\');
					CString _decemfpath = _fileName.Left(offset);
					_decemfpath.Append("\\temp\\");
					if (-1 == GetFileAttributes(_decemfpath.GetBuffer(0)))
						_mkdir(_decemfpath.GetBuffer(0));

					_decemfpath.Append(emfname);
					_decemfpath.Append(ext);

					//EMF文件路径(目录)
					status = AddWaterMark2EMF(_fileName,_decemfpath,strFinalBarCode,NULL,m_strPrinterPath,2,false);
					if((0 !=status))
					{
						GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF()  添加暗水印信息失败！\n",__FILE__, __LINE__);
						goto Exit;
					}
					else
						sprintf(p->filename,"%s",_decemfpath);

				}

			}


			//每页匹配纸张和横纵向
			int papersize = 0;
			short paperorientation = 0;
			//先取得纸张横纵向
			papersize = GetPaperSize(pJobinfo->m_PrintJobInfo.nPrintType,p, &paperorientation,NULL,0);


			//再取得纸张编号,如果调用了打印机驱动的话，则纸张编号和方向根据驱动设置，方向待定
			if (m_HDAppConfig->m_ExConfig.m_nPrinterType == 2)
			{
				papersize = GetPaperSizeByDriver(pJobinfo, devMode, hdcPrint);
				//paperorientation = devMode->dmOrientation;
				if (papersize == 0)
				{
					GenLog(ERROR_INFO,"%s[%d].获取纸张大小为[0]!\n", __FILE__, __LINE__);
					status = -1;
					goto Exit;
				}
			}
			else //if (m_HDAppConfig->m_ExConfig.m_nPrinterType == 1)//普通打印机
			{
				if (!papersize)
				{
					GenLog(ERROR_INFO,"%s[%d].文档 %s 第 %d 页纸张不匹配!!!\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode, p->offset);
					// 网络模式 [10/17/2014 chenhong]
					if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
					{
						GenLog(ERROR_INFO,"%s[%d].文档纸张不匹配!!!\n", __FILE__, __LINE__);
						status = -1;
						goto Exit;
					}
					CString strPaper;
					strPaper.Format(_T("文档 %s 第 %d 页匹配纸张失败！继续将采用打印机驱动设置纸张，是否继续打印？"), pJobinfo->m_PrintJobInfo.szFileName, p->offset);

					if ((IDOK == ShowMsgBox(strPaper.GetBuffer(0), MB_OK)) && (WORKING_NETWORK != HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel))
					{
						papersize = GetPaperSizeByDriver(pJobinfo, devMode, hdcPrint);
						//paperorientation = devMode->dmOrientation;
						if (papersize == 0)
						{
							if (p->offset > pJobinfo->m_JobStatusInfo.m_nStartPage)
							{
								continue;
							}
							else
							{
								status = -1;
								goto Exit;
							}
						}
					}
					else
					{
						if (p->offset > pJobinfo->m_JobStatusInfo.m_nStartPage)
						{
							continue;
						}
						else
						{
							status = -1;
							goto Exit;
						}
					}
				}
			}

			if (((m_nLastPageSize != papersize) || (m_nLastOrientation != paperorientation)) && (PRINTTYPE_PINTYPE != pJobinfo->m_PrintJobInfo.nPrintType))
				//if ((m_nLastPageSize != papersize) || (m_nLastOrientation != paperorientation)) 
			{
				m_nLastPageSize = papersize;
				m_nLastOrientation = paperorientation;

				devMode->dmPaperSize = papersize/*papersize*/;
				devMode->dmPaperWidth = 0;
				devMode->dmPaperLength = 0;
				devMode->dmOrientation = paperorientation;
				devMode->dmScale = 100;//pJobinfo->m_JobStatusInfo.m_nPageScaling;

				devMode->dmCollate = DMCOLLATE_TRUE;

				//ResetDC function can be used to change 
				//the paper orientation or paper bins while printing a document
				if(ResetDC(*hdcPrint, devMode))
				{
					GenLog(DEBUG_INFO, "%s[%d].Reset DC success ,Orientation value =%d , papersize = %d \n",__FILE__,__LINE__,devMode->dmOrientation, devMode->dmPaperSize);
					//重新获取纸张页面偏移量、条码偏移量等信息


				}
				else
				{
					GenLog(ERROR_INFO,"%s[%d].Reset DC failed ,Orientation value =%d , papersize = %d \n",__FILE__,__LINE__,devMode->dmOrientation, devMode->dmPaperSize);
				}

				//设置打印机的默认纸张和方向
				PRINTER_INFO_2 *ppi2 = GetInfo2();
				if (ppi2)
				{
					ppi2->pDevMode->dmFields = DM_PAPERSIZE|DM_PAPERWIDTH|DM_PAPERLENGTH|DM_ORIENTATION;
					ppi2->pDevMode->dmPaperSize = papersize;
					ppi2->pDevMode->dmPaperWidth = 0;
					ppi2->pDevMode->dmPaperLength = 0;
					ppi2->pDevMode->dmOrientation = paperorientation;
					if (SetInfo2(ppi2))
					{
						GenLog(DEBUG_INFO, "%s[%d].设置打印机默认纸张成功！\n",__FILE__,__LINE__);
					}
					else
					{
						GenLog(ERROR_INFO, "%s[%d].设置打印机默认纸张失败！\n",__FILE__,__LINE__);
					}

					GlobalFree((HGLOBAL)ppi2);
				}
			}
			//打印一页内容
			if(m_nFileType == BMP_FILETYPE)
			{
				status = PrintOnePage_BMP(pJobinfo, p, hdcPrint, index);	
			}
			else if(m_nFileType == EMF_FILETYPE)
			{
				////
				////首先判断是否添加暗水印
				////add by zbin 20190523
				////针对EMF文件增加暗水印信息
				////首先先判断是否添加水印信息
				//if(isAddMark && 1 == HDAppConfig::Instance()->m_ExConfig.m_nIsWaterMark)
				//{
				//	//EMF文件添加暗水印信息
				//	//暗水印必须64位 截取15位转换为二进制码
				//	CString strBarCode = pJobinfo->m_szFileBarcode;
				//	int codeLen = strBarCode.GetLength();
				//	if(64 < codeLen)
				//	{
				//		GenLog(ERROR_INFO,"%s[%d]  CHDPrinter::PrintOneDoc()::AddWaterMark2EMF()  暗水印:%s水印长度必须64位！\n",__FILE__, __LINE__,pJobinfo->m_szFileBarcode);
				//		status = -1;
				//		goto Exit;
				//	}else
				//	{
				//		//从后边丢掉预留2位 向前截取15位
				//		CString strTempBarCode = "12345678";
				//		CString strFinalBarCode;
				//		//转码
				//		for (int i = 0 ; i < strTempBarCode.GetLength();i++)
				//		{
				//			//
				//			CString _str(strTempBarCode.GetAt(i));
				//			bitset<4> bitset1(_ttoi(_str.GetBuffer(0)));
				//			std::string strTemp = bitset1.to_string();
				//			strFinalBarCode.Append(strTemp.c_str());
				//		}
				//		//
				//		/*strFinalBarCode.Append("0000");*/
				//		//
				//		//char _path[MAX_PATH] = {0x00}; 
				//		//char drive[3] = {0x00}; 
				//		//_splitpath(_fileName.GetBuffer(0),drive,_path,NULL,NULL);
				//		//int _length_file = _fileName.GetLength();
				//		int offset = _fileName.ReverseFind('\\');
				//		CString _path = _fileName.Left(offset);
				//		_path.Append("\\temp")

				//		//EMF文件路径(目录)
				//		status = AddWaterMark2EMF(_path,strFinalBarCode,m_strPrinterPath);
				//		isAddMark = false;
				//	}	
				// }

				status = PrintOnePage(pJobinfo, p, hdcPrint, index);
			}


			if(status != 0)
			{
				GenLog(DEBUG_INFO, "%s[%d].文档%s打印第%d份第%d页失败!\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szEventCode, index+1, p->offset);
				goto Exit;
			}
			else
			{
				GenLog(ERROR_INFO, "%s[%d].文档%s打印第%d份第%d页成功!\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szEventCode, index+1, p->offset);
				m_nPrintedCounts = index+1;

				*Epage = p->offset;
			}

		}

	}
	else if(m_nFileType == PDF_FILETYPE)
	{

		if(nFlags ==3)
		{
			//xuxing   
			GenLog(DEBUG_INFO, "%s[%d]mupdf 打印\n", __FILE__, __LINE__ );
			TCHAR szFullPath[MAX_PATH] = {0x00}; //执行文件全路径
			GetModuleFileName(NULL , szFullPath , MAX_PATH );
			(_tcsrchr(szFullPath,_T('\\')))[1] = 0;

			//获取文件名称，截取文件类型
			TCHAR szExt[10] = {0x00};
			TCHAR szFileName[256] = {0x00};
			_tsplitpath(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, NULL, NULL, szFileName, szExt);


			TCHAR szDocName[256] = {0x00};
			sprintf(szDocName,"HDPrintConsole-%s#Document-%s$.pdf", m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0), pJobinfo->m_szFileBarcode);

			char caInBarCodePath[1024] = {0}; //条码图片路径
			char caInBarCodeDesc[1024] = {0}; //条码描述


			//DeleteFile(csOutPdfFilePath);

			//生成条码图片
			if(0 != CreatBarCodeGraph(pJobinfo , caInBarCodePath))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成图片失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}

			//生成条码描述
			if( 0 != CreatBarCodeDesc(pJobinfo , caInBarCodeDesc,index+1))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成条码描述失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}
			//mupdf 打印
			MupdfContext* ctx = NULL;
			MupdfDocument* doc = NULL;

			// 初始化
			mupdf_init(&ctx);

			// 打开 PDF
			if (mupdf_open_document(ctx, pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, &doc) != 0) {
				printf("错误: %s\n", mupdf_get_error(ctx));
				mupdf_fini(ctx);
				return 1;
			}
			if(strcmp(caInBarCodePath,"None")!=0)//
			{
				//条码图片路径
				CString csInBarCodePath(caInBarCodePath);
				//条码描述
				CString csInBarCodeDesc(caInBarCodeDesc);

				int n_position = pJobinfo->m_PrintJobInfo.nPosition;
				int n_page = pJobinfo->m_PrintJobInfo.nPerPage;

				CString position;
				position.Format(_T("%d"),n_position);
				CString page;
				page.Format(_T("%d"),n_page);

				//条码长度、高
				int nBarHigh = HDAppConfig::Instance()->m_ExConfig.m_nBarcodeHigh;
				int nBarLen = HDAppConfig::Instance()->m_ExConfig.m_nBarcodeLen;
				//条码偏移量
				int nBarX = HDAppConfig::Instance()->m_ExConfig.m_nBarcodex;
				int nBarY = HDAppConfig::Instance()->m_ExConfig.m_nBarcodey;
				int text_gap = HDAppConfig::Instance()->m_ExConfig.m_text_gap;
				int text_width_scale = HDAppConfig::Instance()->m_ExConfig.m_text_width_scale;
				int b5_offset_x = HDAppConfig::Instance()->m_ExConfig.m_b5_offset_x;
				int use_freetext_a4 = HDAppConfig::Instance()->m_ExConfig.m_use_freetext_a4;
				// 获取页数
				int total = 0;
				mupdf_get_page_count(ctx, doc, &total);
				printf("PDF 共 %d 页\n", total);

				//LayoutRule .type 0 绝对坐标 ；1左上；2 右上；3 左下； 4右下
				// ---- 添加文字水印 ----
				//LayoutRule text_rule = {0};
				//text_rule.type = pJobinfo->m_PrintJobInfo.nPosition;    
				//text_rule.margin_x = nBarX;
				//text_rule.margin_y = nBarY+70;
				//text_rule.add_page_number = 1; 

				//int added_text = mupdf_batch_add_text(ctx, doc, NULL, -1,
				//	caInBarCodeDesc, 10.0f, &text_rule);
				//printf("已添加文字水印: %d 页\n", added_text);

				//// ---- 添加图片水印 ----
				//LayoutRule img_rule = {0};
				//img_rule.type = pJobinfo->m_PrintJobInfo.nPosition; 
				//img_rule.margin_x = nBarX;
				//img_rule.margin_y = nBarY;

				//int added_img = mupdf_batch_add_image(ctx, doc, NULL, -1,
				//	caInBarCodePath, &img_rule, nBarLen, nBarHigh);

				LayoutRule rule = {0};
				//rule.type = 5;
				rule.type =pJobinfo->m_PrintJobInfo.nPosition; 
				rule.margin_x = nBarX+10;
				rule.margin_y =nBarY+20;
				rule.add_page_number = 1;
				rule.text_gap = 5+text_gap;
				rule.font_path = "C:\\Windows\\Fonts\\simsun.ttc";
				GenLog(DEBUG_INFO, "%s[%d]font_path[%s] \n", __FILE__, __LINE__,rule.font_path);
				rule.font_index =0;
				rule.text_width_scale = text_width_scale;
				rule.b5_offset_x = b5_offset_x;
				rule.use_freetext_a4 = use_freetext_a4;

				GenLog(DEBUG_INFO, "%s[%d]b5_offset_x[%d] use_freetext_a4[%d] \n", __FILE__, __LINE__,rule.b5_offset_x,rule.use_freetext_a4 );

				mupdf_batch_add_image(ctx, doc, NULL, -1, caInBarCodePath, &rule, nBarLen, nBarHigh);
				mupdf_batch_add_text(ctx, doc, NULL, -1,caInBarCodeDesc, 10.0f, &rule);

				
				//nColor;          //打印色彩（0未定义；1黑白；2彩色）
				//int  nPrintDouble;    //打印方式(双面/单面)，1:单面；2:双面纵翻;3:双面横翻
				//if(1 == pJobinfo->m_PrintJobInfo.nPrintDouble);

				PrintOptions opts = {0};
				opts.copies = 1;
				opts.duplex =  pJobinfo->m_PrintJobInfo.nPrintDouble;        
				opts.color = pJobinfo->m_PrintJobInfo.nColor;          
				opts.scale = 0;           
				opts.from_page = 0;
				opts.to_page = 0;
				opts.job_name = szDocName;
			//	if( mupdf_save_document(ctx, doc, "mupdf_out.pdf")==0)
			//	{
			//		GenLog(DEBUG_INFO, "%s[%d]mupdf_out save ok\n", __FILE__, __LINE__ );
			//	}
			//	GenLog(ERROR_INFO,"%s[%d].nPrintDouble %d; nColor %d !!!\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.nPrintDouble,pJobinfo->m_PrintJobInfo.nColor);
				int ret = mupdf_print(ctx, doc, NULL,&opts);

				GenLog(DEBUG_INFO, "%s[%d]mupdf_print end \n", __FILE__, __LINE__);
				mupdf_close_document(ctx, doc);
				mupdf_fini(ctx);
				GenLog(DEBUG_INFO, "%s[%d]mupdf_print out \n", __FILE__, __LINE__);

			}
			else//无条码
			{
				PrintOptions opts = {0};
				opts.copies = 1;
				opts.duplex =  pJobinfo->m_PrintJobInfo.nPrintDouble;         //  1=单面, 2=长边翻转(双面), 3=短边翻转(双面)
				opts.color = pJobinfo->m_PrintJobInfo.nColor;           // 1=黑白, 2=彩色
				opts.scale = 0;           //
				opts.from_page = 0;
				opts.to_page = 0;
				opts.job_name = szDocName;

				GenLog(ERROR_INFO,"%s[%d].nPrintDouble %d; nColor %d !!!\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.nPrintDouble,pJobinfo->m_PrintJobInfo.nColor);
				int ret = mupdf_print(ctx, doc, NULL,&opts);

				mupdf_close_document(ctx, doc);
				mupdf_fini(ctx);
			}
		}
		else if(nFlags ==2 )
		{
			GenLog(DEBUG_INFO, "%s[%d]大文件打印，m_nFileType == PDF_FILETYPE\n", __FILE__, __LINE__ );
			/*WinExec(_T("taskkill -f -im SetPrinter.exe -im syssrv.exe -im hdinjdlls.exe -im hdinjdlls32.exe "),SW_HIDE);
			Sleep(500);	
			SetDefaultPrinter(m_strPrinterPath.GetBuffer());*/

			TCHAR szFullPath[MAX_PATH] = {0x00}; //执行文件全路径
			GetModuleFileName(NULL , szFullPath , MAX_PATH );
			(_tcsrchr(szFullPath,_T('\\')))[1] = 0;

			//获取文件名称，截取文件类型
			TCHAR szExt[10] = {0x00};
			TCHAR szFileName[256] = {0x00};
			_tsplitpath(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, NULL, NULL, szFileName, szExt);


			TCHAR szDocName[256] = {0x00};
			sprintf(szDocName,"HDPrintConsole-%s#Document-%s$.pdf", m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0), pJobinfo->m_szFileBarcode);
			CString csInWordFilePath;
			//CString csInBarCodePath;
			CString csOutPdfFilePath;
			CString csOuthdPdfFilePath;

			char caInBarCodePath[1024] = {0}; //条码图片路径
			char caInBarCodeDesc[1024] = {0}; //条码描述
			char caInBarcode[1024] = {0}; //条码号
			csInWordFilePath = pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename;

			csOutPdfFilePath = CHDDataCenter::Instance()->GetDirectory(1);
			csOuthdPdfFilePath = csOutPdfFilePath + szDocName;//输出文件匹配航盾规则
			csOutPdfFilePath = csOutPdfFilePath + szFileName;
			csOutPdfFilePath = csOutPdfFilePath + ".pdf";

			//DeleteFile(csOutPdfFilePath);

			//生成条码图片
			if(0 != CreatBarCodeGraph(pJobinfo , caInBarCodePath))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成图片失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}

			//生成条码描述
			if( 0 != CreatBarCodeDesc(pJobinfo , caInBarCodeDesc,index+1))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成条码描述失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}


			if(strcmp(caInBarCodePath,"None")!=0)
			{
				
				//条码图片路径
				CString csInBarCodePath(caInBarCodePath);
				//条码描述
				CString csInBarCodeDesc(caInBarCodeDesc);



				int n_position = pJobinfo->m_PrintJobInfo.nPosition;
				int n_page = pJobinfo->m_PrintJobInfo.nPerPage;

				CString position;
				position.Format(_T("%d"),n_position);
				CString page;
				page.Format(_T("%d"),n_page);

				//条码长度、高
				int nBarHigh = HDAppConfig::Instance()->m_ExConfig.m_nBarcodeHigh;
				int nBarLen = HDAppConfig::Instance()->m_ExConfig.m_nBarcodeLen;
				//条码偏移量
				int nBarX = HDAppConfig::Instance()->m_ExConfig.m_nBarcodex;
				int nBarY = HDAppConfig::Instance()->m_ExConfig.m_nBarcodey;
				int text_gap = HDAppConfig::Instance()->m_ExConfig.m_text_gap;
				int text_width_scale = HDAppConfig::Instance()->m_ExConfig.m_text_width_scale;

				CString cLen;
				cLen.Format(_T("%d"),nBarLen);

				CString cHigh;
				cHigh.Format(_T("%d"),nBarHigh);

				CString cX;
				cX.Format(_T("%d"),nBarX);

				CString cY;
				cY.Format(_T("%d"),nBarY);
				CString a;

				//a = "\""+csInWordFilePath+"\""+" "+"\""+csInBarCodePath+"\""+" "+position+" "+page+" "+"\""+csOutPdfFilePath+"\""+" "+"\""+caInBarCodeDesc+"\"";
				if(IsWow64())
				{
					a ="java -jar -Xms3072m -Xmn1024m  pdf.jar ";
				}
				else
				{
					a ="java -jar -Xms1024m -Xmn512m  pdf.jar ";
				}
				int barcode_tips = HDAppConfig::Instance()->m_ExConfig.m_barcode_tips;
				if((barcode_tips==1)&&(pJobinfo->m_PrintJobInfo.nSeclvCode==5))
				{
					CString csInSecCode="内部";

					a=a+"\""+csInWordFilePath+"\""+" "+"\""+csInBarCodePath+"\""+" "+position+" "+page+" "+"\""+csOuthdPdfFilePath+"\""+" "+"\""+csInBarCodeDesc+"\""+" " +cX+" "+ cY+" " +cLen+" "+cHigh+" 0 "+csInSecCode;
				}
				else
					a=a+"\""+csInWordFilePath+"\""+" "+"\""+csInBarCodePath+"\""+" "+position+" "+page+" "+"\""+csOuthdPdfFilePath+"\""+" "+"\""+csInBarCodeDesc+"\""+" " +cX+" "+ cY+" " +cLen+" "+cHigh+" 0";

				GenLog(DEBUG_INFO, "%s[%d]大文件打印，打印输出值为 :[%s] [%s] [%d] [%d] [%s] [%s][%d] [%d] [%d] [%d]\n", __FILE__, __LINE__ , csInWordFilePath,csInBarCodePath, position,page,csOuthdPdfFilePath,caInBarCodeDesc,nBarX,nBarY,nBarLen,nBarHigh);
				FILE *f;

				char cmdstr[2048];
				//sprintf(cmdstr,"@echo off\n");
				sprintf(cmdstr,"%s\ntimeout /t 2\n",a.GetBuffer(0));
				//sprintf(cmdstr,"%s\n\n",a.GetBuffer(0));
				f=fopen("C:\\tmp.bat","w");
				fprintf(f,"%s",cmdstr);
				fclose(f);
				char wCmd[1024] = {0};
				wchar_t wFileName[MAX_PATH] = {0};
				sprintf(wCmd,"C:\\tmp.bat");
				//起进程
				STARTUPINFO si;
				PROCESS_INFORMATION pi;

				ZeroMemory( &si, sizeof(si) );
				si.cb = sizeof(si);
				si.dwFlags = STARTF_USESHOWWINDOW;
				si.wShowWindow = SW_HIDE;
				ZeroMemory( &pi, sizeof(pi) );


				// Start the child process. 
				if( !CreateProcess( NULL,   // No module name (use command line)
					wCmd,       // Command line
					NULL,           // Process handle not inheritable
					NULL,           // Thread handle not inheritable
					FALSE,          // Set handle inheritance to FALSE
					0,              // No creation flags
					NULL,           // Use parent's environment block
					NULL,           // Use parent's starting directory 
					&si,            // Pointer to STARTUPINFO structure
					&pi )           // Pointer to PROCESS_INFORMATION structure
					) 
				{
					int err = GetLastError();
					return 0;
				}

				// Wait until child process exits.
				WaitForSingleObject( pi.hProcess, INFINITE );

				// Close process and thread handles. 
				CloseHandle( pi.hProcess );
				CloseHandle( pi.hThread );

				GenLog(DEBUG_INFO, "%s[%d]条码添加完成\n", __FILE__, __LINE__);
				FILE * file;
				file =fopen(csOuthdPdfFilePath.GetBuffer(0),"r");
				if(file==NULL)
				{

					GenLog(DEBUG_INFO, "%s[%d]条码添加异常\n", __FILE__, __LINE__);
					FILE *f;
					char cmdstr[2048];
					//sprintf(cmdstr,"@echo off\n");
					sprintf(cmdstr,"%s\ntimeout /t 10\n",a.GetBuffer(0));
					//sprintf(cmdstr,"%s\n\n",a.GetBuffer(0));
					f=fopen("C:\\tmp.bat","w");
					fprintf(f,"%s",cmdstr);
					fclose(f);

					char wCmd[1024] = {0};
					sprintf(wCmd,"C://tmp.bat");
					//起进程
					STARTUPINFO si;
					PROCESS_INFORMATION pi;

					ZeroMemory( &si, sizeof(si) );
					si.cb = sizeof(si);
					//si.dwFlags = STARTF_USESHOWWINDOW;
					//si.wShowWindow = SW_HIDE;
					ZeroMemory( &pi, sizeof(pi) );


					// Start the child process. 
					if( !CreateProcess( NULL,   // No module name (use command line)
						wCmd,       // Command line
						NULL,           // Process handle not inheritable
						NULL,           // Thread handle not inheritable
						FALSE,          // Set handle inheritance to FALSE
						0,              // No creation flags
						NULL,           // Use parent's environment block
						NULL,           // Use parent's starting directory 
						&si,            // Pointer to STARTUPINFO structure
						&pi )           // Pointer to PROCESS_INFORMATION structure
						) 
					{
						int err = GetLastError();
						return 0;
					}

					// Wait until child process exits.
					WaitForSingleObject( pi.hProcess, INFINITE );

					// Close process and thread handles. 
					CloseHandle( pi.hProcess );
					CloseHandle( pi.hThread );

					system(a.GetBuffer(0));
					FILE * file;
					file =fopen(csOuthdPdfFilePath.GetBuffer(0),"r");
					if(file==NULL)
					{
						ShowMsgBox("添加条码失败", MB_OK);

						status = -1;
						goto Exit;
					}
					else 
						fclose(file);


				}
				else 
					fclose(file);

				GenLog(DEBUG_INFO, "%s[%d]条码添加完成\n", __FILE__, __LINE__);
				csInWordFilePath.ReleaseBuffer();
				csInBarCodePath.ReleaseBuffer();


				//CopyFile(csOutPdfFilePath.GetBuffer(0),csOuthdPdfFilePath.GetBuffer(0),FALSE);
			}
			else
			{

				CopyFile(csInWordFilePath.GetBuffer(0),csOuthdPdfFilePath.GetBuffer(0),FALSE);

			}
			GenLog(DEBUG_INFO, "%s[%d]CopyFile[%s][%s] \n", __FILE__, __LINE__ , csInWordFilePath.GetBuffer(),csOuthdPdfFilePath.GetBuffer(0) );

			int nPrintDouble = pJobinfo->m_PrintJobInfo.nPrintDouble;
			int nColors = pJobinfo->m_PrintJobInfo.nColor;
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 单双面为: %d \n", __FILE__, __LINE__, nPrintDouble);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 色彩为: %d \n", __FILE__, __LINE__, nColors);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 纸张尺寸为: %s \n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szPageSize);

			//add by zkx 修改打印机首选项
           DWORD dwNeeded=0;
		   bool bflag;
		 HANDLE      PDFprinter = NULL;
	     PRINTER_DEFAULTS pDefault;
	     pDefault.DesiredAccess = PRINTER_ALL_ACCESS;
	     pDefault.pDatatype = NULL;
	     pDefault.pDevMode = NULL;
		 //获取打印机句柄
    	 if (!OpenPrinter((LPSTR)m_strPrinterPath.GetBuffer(0), &PDFprinter, &pDefault)) 
    	 {
		  GenLog(ERROR_INFO,"%s[%d].文档%s打开打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
	   	 status = -1;
		  goto Exit;
	     }
		 //获取打印机 信息PRINTER_INFO_2结构体字节长度
		   bflag  =GetPrinter(PDFprinter,2,0,0,&dwNeeded);
		   if((!bflag)&&(GetLastError()!=ERROR_INSUFFICIENT_BUFFER) ||(dwNeeded==0))
		   {
		     GenLog(ERROR_INFO,"%s[%d].文档%sGetPrinter打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
			 ClosePrinter(PDFprinter);
			  PDFprinter = NULL;
			 status = -1;
			  goto Exit;
		   }
		   //分配内存空间
		   PRINTER_INFO_2* hdpi2=NULL;
		   hdpi2=(  PRINTER_INFO_2*)GlobalAlloc(GPTR,dwNeeded);
		    if( hdpi2==NULL)
		   {
			   GenLog(ERROR_INFO,"%s[%d].文档%sGlobalAlloc%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
			    ClosePrinter(PDFprinter);
			  PDFprinter = NULL;
			  status = -1;
			  goto Exit;
		   }
			//获取详细打印机信息  PRINTER_INFO_2
			 bflag  =GetPrinter(PDFprinter,2, (LPBYTE) hdpi2, dwNeeded,&dwNeeded);
			 if(!bflag)
		   {
            GenLog(ERROR_INFO,"%s[%d].文档%sGetPrinter2打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
             GlobalFree( hdpi2);
			  ClosePrinter(PDFprinter);
			  PDFprinter = NULL;
			 status = -1;
			  goto Exit;
		   }
			 //将配置的devMode（在函数 SetPrinterParam设置以文件第一页为准）设置到打印机信息结构体（devMode定义打印机默认数据）
			 hdpi2->pDevMode=devMode;
			 hdpi2->pDevMode->dmDuplex=nPrintDouble;
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 hdpi2->pDevMode->dmColor: %d \n", __FILE__, __LINE__, hdpi2->pDevMode->dmColor);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印  hdpi2->pDevMode->dmPaperSize: %d \n", __FILE__, __LINE__,hdpi2->pDevMode->dmPaperSize);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印  hdpi2->pDevMode->dmDuplex: %d \n", __FILE__, __LINE__, hdpi2->pDevMode->dmDuplex);

			//将更新的配置载入打印机
			 bflag  =SetPrinter(PDFprinter,2, (LPBYTE) hdpi2, 0);
			  if(!bflag)
		   {
             GenLog(ERROR_INFO,"%s[%d].文档%sSetPrinter%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
             GlobalFree( hdpi2);
			  ClosePrinter(PDFprinter);
			  PDFprinter = NULL;
			 status = -1;
			  goto Exit;
		   }
			//使得打印配置修改生效，以广播的方式发送系统的所有的顶层窗口
			  SendMessageTimeout(HWND_BROADCAST,WM_DEVMODECHANGE,0L,(LPARAM)(LPCSTR)m_strPrinterPath.GetBuffer(),SMTO_NORMAL,100,NULL);
              GlobalFree( hdpi2);
			   ClosePrinter(PDFprinter);
			  PDFprinter = NULL;
			/*
			if(nColors == 1)
			{
			if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "A4") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printoneA4.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【黑白】打印机首选项A4单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleLRA4.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【黑白】打印机首选项A4双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleUDA4.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【黑白】打印机首选项A4双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}
			else if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "A3") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printoneA3.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【黑白】打印机首选项A3单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleLRA3.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【黑白】打印机首选项A3双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleUDA3.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【黑白】打印机首选项A3双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}
			else if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "A5") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printoneA5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【黑白】打印机首选项A5单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleLRA5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【黑白】打印机首选项A5双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleUDA5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【黑白】打印机首选项A5双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}
			else if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "B5") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printoneB5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【黑白】打印机首选项B5单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleLRB5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【黑白】打印机首选项B5双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoubleUDB5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【黑白】打印机首选项B5双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}

			}
			else if(nColors == 2)
			{
			if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "A4") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printonecolorA4.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【彩色】打印机首选项A4单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorLRA4.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【彩色】打印机首选项A4双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorUDA4.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件PDF打印，还原【彩色】打印机首选项A4双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}
			else  if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "A3") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printonecolorA3.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项A3单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorLRA3.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项A3双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorUDA3.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项A3双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}
			else  if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "A5") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printonecolorA5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项A5单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorLRA5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项A5双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorUDA5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项A5双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}
			else  if(strcmp(pJobinfo->m_PrintJobInfo.szPageSize, "B5") == 0)
			{
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printonecolorB5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项B5单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			} 
			else if (nPrintDouble == 2)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorLRB5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项B5双面左右模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			else if (nPrintDouble == 3)
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\HDPrinter\\%s_printdoublecolorUDB5.dat\" u", m_strPrinterPath.GetBuffer(0),m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].大文件打印，还原【彩色】打印机首选项B5双面上下模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(500);
			}
			}

			}
			*/
			// 输出pdf文件
			//ShowMsgBox(_T("提示：PDF大文件打印时，请在本份作业打印出纸全部完毕后，再打印下一份作业。\n如果有Adobe Reader程序启动，请手动关闭，否则会影响台账记录，谢谢合作！"), MB_OKCANCEL);
			CString csPrintPdf;
			int pdfversion = HDAppConfig::Instance()->m_ExConfig.m_pdfversion;
			GenLog(ERROR_INFO, "%s[%d].pdf版本为：pdfversion = %d ，1为PDF10，0为PDF9！\n", __FILE__, __LINE__, pdfversion);
			if (1 == pdfversion)
			{
				csPrintPdf.Format("start acrord32 /p /h \"%s\"", csOuthdPdfFilePath.GetBuffer());
				GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			} 
			else
			{
				csPrintPdf.Format("start acrobat /p /h \"%s\"", csOuthdPdfFilePath.GetBuffer());
				GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());

			}
			NprinterNum++;
			GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%d!\n",__FILE__,__LINE__, NprinterNum);
			system(csPrintPdf.GetBuffer());
			csPrintPdf.ReleaseBuffer(); 
			csOutPdfFilePath.ReleaseBuffer();
			
			Sleep(10000);
			//WinExec(_T("taskkill -f -im acrobat.exe -im acrord32.exe"),SW_HIDE);
			//Sleep(500);

			GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
		}
		else if (nFlags == 1)
		{
			WinExec(_T("taskkill -f -im SetPrinter.exe "),SW_HIDE);
			Sleep(500);	
			SetDefaultPrinter(m_strPrinterPath.GetBuffer());
			TCHAR szFullPath[MAX_PATH] = {0x00}; //执行文件全路径
			GetModuleFileName(NULL , szFullPath , MAX_PATH );
			(_tcsrchr(szFullPath,_T('\\')))[1] = 0;

			//获取文件名称，截取文件类型
			TCHAR szExt[10] = {0x00};
			TCHAR szFileName[256] = {0x00};
			_tsplitpath(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, NULL, NULL, szFileName, szExt);

			TCHAR szDocName[256] = {0x00};
			sprintf(szDocName,"HDPrintConsole-%s#Document-%s$", m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0), pJobinfo->m_szFileBarcode);
			CString csInWordFilePath;
			//CString csInBarCodePath;
			CString csOutPdfFilePath;
			char caInBarCodePath[1024] = {0}; //条码图片路径
			char caInBarCodeDesc[1024] = {0}; //条码描述
			char caInBarcode[1024] = {0}; //条码号
			csInWordFilePath = pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename;
			csOutPdfFilePath = CHDDataCenter::Instance()->GetDirectory(1);
			csOutPdfFilePath = csOutPdfFilePath + szFileName;
			csOutPdfFilePath = csOutPdfFilePath + ".pdf";



			CString csCmdOrder(szFullPath);
			//csCmdOrder = szFullPath;
			csCmdOrder = csCmdOrder + "pdfcreate-new.exe";
			//生成条码图片
			if(0 != CreatBarCodeGraph(pJobinfo , caInBarCodePath))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成图片失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}
			//生成条码描述
			if( 0 != CreatBarCodeDesc(pJobinfo , caInBarCodeDesc,index+1))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成条码描述失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}

			//条码图片路径
			CString csInBarCodePath(caInBarCodePath);
			//条码描述
			CString csInBarCodeDesc(caInBarCodeDesc);

			csCmdOrder = "\"" + csCmdOrder + "\" \"";
			csCmdOrder = csCmdOrder + csInWordFilePath + "\" \"";
			csCmdOrder = csCmdOrder + csInBarCodePath + "\" \"";
			csCmdOrder = csCmdOrder + csOutPdfFilePath + "\" \"";
			csCmdOrder = csCmdOrder + csInBarCodeDesc + "\"";
			//页数
			switch(pJobinfo->m_PrintJobInfo.nPerPage)
			{
			case 1:
				csCmdOrder = csCmdOrder + " Home";
				break;
			case 2:
				csCmdOrder = csCmdOrder + " Trailing";
				break;
			case 3:
				csCmdOrder = csCmdOrder + " All";
				break;
			default:
				GenLog(DEBUG_INFO, "%s[%d]选择页数 error! \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;		
			}

			//位置和大小
			switch(pJobinfo->m_PrintJobInfo.nPosition)
			{
			case 0:
				csCmdOrder = csCmdOrder + " 0.3 0.26 0.330";
				break;
			case 1:
				csCmdOrder = csCmdOrder + " 0.3 0.012 0.330";
				break;;
			case 2:
				csCmdOrder = csCmdOrder + " 0.3 0.26 0.008";
				break;
			case 3:
				csCmdOrder = csCmdOrder + " 0.3 0.012 0.008";
				break;
			case 4:
				csCmdOrder = csCmdOrder + " 0.3 0.14 0.330";
				break;
			case 5:
				csCmdOrder = csCmdOrder + " 0.3 0.14 0.008";
				break;
			default:
				GenLog(DEBUG_INFO, "%s[%d]选择页数 error! \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;		
			}

			GenLog(DEBUG_INFO, "%s[%d]EexShell1 csCmdOrder :[%s] \n", __FILE__, __LINE__ , csCmdOrder.GetBuffer() );

			//执行命令
			char result[1024] = {0};
			if( 0 != ExeShell(csCmdOrder.GetBuffer() , result))
			{
				GenLog(DEBUG_INFO, "%s[%d]EexShell1 error! csCmdOrder :[%s] result :[%s]\n", __FILE__, __LINE__ , csCmdOrder.GetBuffer() ,result );
				status = -1;
				goto Exit;
			}
			//判断文件加条码是否成功
			if(!strstr(result , "SUCC"))
			{
				GenLog(DEBUG_INFO, "%s[%d]EexShell2 error! csCmdOrder :[%s] result :[%s]\n", __FILE__, __LINE__ , csCmdOrder.GetBuffer() ,result);
				status = -1;
				goto Exit;
			}
			csInWordFilePath.ReleaseBuffer();
			csInBarCodePath.ReleaseBuffer();
			csCmdOrder.ReleaseBuffer();

			//add by zkx 修改打印机首选项
			DWORD dwNeeded=0;
			bool bflag;
			HANDLE      PDFprinter = NULL;
			PRINTER_DEFAULTS pDefault;
			pDefault.DesiredAccess = PRINTER_ALL_ACCESS;
			pDefault.pDatatype = NULL;
			pDefault.pDevMode = NULL;
			//获取打印机句柄
			if (!OpenPrinter((LPSTR)m_strPrinterPath.GetBuffer(0), &PDFprinter, &pDefault)) 
			{
				GenLog(ERROR_INFO,"%s[%d].文档%s打开打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
				status = -1;
				goto Exit;
			}
			//获取打印机 信息PRINTER_INFO_2结构体字节长度
			bflag  =GetPrinter(PDFprinter,2,0,0,&dwNeeded);
			if((!bflag)&&(GetLastError()!=ERROR_INSUFFICIENT_BUFFER) ||(dwNeeded==0))
			{
				GenLog(ERROR_INFO,"%s[%d].文档%sGetPrinter打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
				ClosePrinter(PDFprinter);
				PDFprinter = NULL;
				status = -1;
				goto Exit;
			}
			//分配内存空间
			PRINTER_INFO_2* hdpi2=NULL;
			hdpi2=(  PRINTER_INFO_2*)GlobalAlloc(GPTR,dwNeeded);
			if( hdpi2==NULL)
			{
				GenLog(ERROR_INFO,"%s[%d].文档%sGlobalAlloc%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
				ClosePrinter(PDFprinter);
				PDFprinter = NULL;
				status = -1;
				goto Exit;
			}
			//获取详细打印机信息  PRINTER_INFO_2
			bflag  =GetPrinter(PDFprinter,2, (LPBYTE) hdpi2, dwNeeded,&dwNeeded);
			if(!bflag)
			{
				GenLog(ERROR_INFO,"%s[%d].文档%sGetPrinter2打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
				GlobalFree( hdpi2);
				ClosePrinter(PDFprinter);
				PDFprinter = NULL;
				status = -1;
				goto Exit;
			}
			//将配置的devMode（在函数 SetPrinterParam设置以文件第一页为准）设置到打印机信息结构体（devMode定义打印机默认数据）
			hdpi2->pDevMode=devMode;
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 hdpi2->pDevMode->dmColor: %d \n", __FILE__, __LINE__, hdpi2->pDevMode->dmColor);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印  hdpi2->pDevMode->dmPaperSize: %d \n", __FILE__, __LINE__,hdpi2->pDevMode->dmPaperSize);
			GenLog(DEBUG_INFO, "%s[%d]大文件打印  hdpi2->pDevMode->dmDuplex: %d \n", __FILE__, __LINE__, hdpi2->pDevMode->dmDuplex);

			//将更新的配置载入打印机
			bflag  =SetPrinter(PDFprinter,2, (LPBYTE) hdpi2, 0);
			if(!bflag)
			{
				GenLog(ERROR_INFO,"%s[%d].文档%sSetPrinter%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
				GlobalFree( hdpi2);
				ClosePrinter(PDFprinter);
				PDFprinter = NULL;
				status = -1;
				goto Exit;
			}
			//使得打印配置修改生效，以广播的方式发送系统的所有的顶层窗口
			SendMessageTimeout(HWND_BROADCAST,WM_DEVMODECHANGE,0L,(LPARAM)(LPCSTR)m_strPrinterPath.GetBuffer(),SMTO_NORMAL,100,NULL);
			GlobalFree( hdpi2);
			ClosePrinter(PDFprinter);
			PDFprinter = NULL;
			//ShowTipMsg(_T("提示：请等待此任务打印完成，不要重复刷卡"), 3);
			/*Sleep(1*1000);
			int nPrintDouble = pJobinfo->m_PrintJobInfo.nPrintDouble;
			GenLog(DEBUG_INFO, "%s[%d]PDF打印 单双面为: %d \n", __FILE__, __LINE__, nPrintDouble);
			if (nPrintDouble == 1)
			{
			// 还原单面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\printone.dat\" u", m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].PDF打印，还原打印机首选项单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(1*1000);
			} 
			else
			{
			// 还原双面面模板 [10/31/2018 Administrator]
			TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\printdouble.dat\" u", m_strPrinterPath.GetBuffer(0));
			GenLog(ERROR_INFO, "%s[%d].PDF打印，还原打印机首选项双面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			WinExec(szCmdbufferPro,SW_HIDE);
			Sleep(1*1000);
			}
			*/
			CString csPrintPdf;
			int pdfversion = HDAppConfig::Instance()->m_ExConfig.m_pdfversion;
			GenLog(ERROR_INFO, "%s[%d].pdf版本为：pdfversion = %d ，1为PDF10，0为PDF9！\n", __FILE__, __LINE__, pdfversion);
			if (1 == pdfversion)
			{
				csPrintPdf.Format("start acrord32 /p /h \"%s\"", csOutPdfFilePath.GetBuffer());
				GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			} 
			else
			{
				csPrintPdf.Format("start acrobat /p /h \"%s\"", csOutPdfFilePath.GetBuffer());
				GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			}
			GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			char recovery_dir[1024] = {0};
			char syspath[1024] = {0};
			char cmdstr[2014]={0};
			char path[1024];
			FILE *f;
			sprintf(cmdstr,"@echo off\n");
			sprintf(cmdstr,"%s%s\n",cmdstr,csPrintPdf.GetBuffer());
			f=fopen("C:\\tmp.bat","w");
			fprintf(f,"%s",cmdstr);
			fclose(f);
			/*system("tmp.bat");
			remove("tmp.bat");*/
			wchar_t wMsg[1024] = {0};
			wchar_t *wCmd;
			wchar_t wFileName[MAX_PATH] = {0};
			sprintf(path,"C:\\tmp.bat");
			//起进程
			STARTUPINFO si;
			PROCESS_INFORMATION pi;

			ZeroMemory( &si, sizeof(si) );
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;
			ZeroMemory( &pi, sizeof(pi) );


			// Start the child process. 
			if( !CreateProcess( NULL,   // No module name (use command line)
				path,       // Command line
				NULL,           // Process handle not inheritable
				NULL,           // Thread handle not inheritable
				FALSE,          // Set handle inheritance to FALSE
				0,              // No creation flags
				NULL,           // Use parent's environment block
				NULL,           // Use parent's starting directory 
				&si,            // Pointer to STARTUPINFO structure
				&pi )           // Pointer to PROCESS_INFORMATION structure
				) 
			{
				int err = GetLastError();

				GenLog(DEBUG_INFO, "%s[%d].输出PDFCreateProcess:%d!\n",__FILE__,__LINE__,err);
			}
			else
			{
				GenLog(DEBUG_INFO, "%s[%d].输出PDFCreateProcess success!\n",__FILE__,__LINE__);

			}

			// Wait until child process exits.
			WaitForSingleObject( pi.hProcess, INFINITE );

			// Close process and thread handles. 
			CloseHandle( pi.hProcess );
			CloseHandle( pi.hThread );
			//system(csPrintPdf.GetBuffer());			
			//csPrintPdf.ReleaseBuffer();

			Sleep(1000*10);
			//WinExec(_T("taskkill -f -im AcroRd32.exe -im acrobat.exe"),SW_HIDE);
			Sleep(1*1000);	

			GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			//if (nPrintDouble != 1)
			//{
			//	// 还原单面模板 [10/31/2018 Administrator]
			//	TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			//	sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\printone.dat\" u", m_strPrinterPath.GetBuffer(0));
			//	GenLog(ERROR_INFO, "%s[%d].PDF打印，打印完成后，默认还原成单面模板，命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			//	WinExec(szCmdbufferPro,SW_HIDE);
			//	Sleep(1*1000);
			//}

		} 
		else
		{
			int ntemcount = m_pEngine->PageCount();
			if(pJobinfo->m_JobStatusInfo.m_nEndPage < ntemcount)
			{
				nPageCount = pJobinfo->m_JobStatusInfo.m_nEndPage;
			}
			else
			{
				nPageCount = ntemcount;
			}
			for (DWORD pageNo = 1; pageNo < nPageCount+1;pageNo++)
			{
				//if(pJobinfo->m_JobStatusInfo.m_nStartPage>pageNo)
				//	continue;

				//if(pJobinfo->m_JobStatusInfo.m_nEndPage < pageNo)
				//	break;


				//每页匹配纸张和横纵向
				int papersize = 0;
				short paperorientation = 0;
				//先取得纸张横纵向
				p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList);
				papersize = GetPaperSize(pJobinfo->m_PrintJobInfo.nPrintType,p, &paperorientation,hdcPrint,pageNo);
				GenLog(ERROR_INFO,"%s[%d].m_nLastPageSize%d;papersize:%d;m_nLastOrientation=%d;paperorientation:%d!!!\n", __FILE__, __LINE__,m_nLastPageSize,papersize,m_nLastOrientation,paperorientation);

				//再取得纸张编号,如果调用了打印机驱动的话，则纸张编号和方向根据驱动设置，方向待定
				if (m_HDAppConfig->m_ExConfig.m_nPrinterType == 2)
				{
					papersize = GetPaperSizeByDriver(pJobinfo, devMode, hdcPrint);
					//paperorientation = devMode->dmOrientation;
					if (papersize == 0)
					{
						GenLog(ERROR_INFO,"%s[%d].获取纸张大小为[0]!\n", __FILE__, __LINE__);
						status = -1;
						goto Exit;
					}
				}
				else //if (m_HDAppConfig->m_ExConfig.m_nPrinterType == 1)//普通打印机
				{
					if (!papersize)
					{
						GenLog(ERROR_INFO,"%s[%d].文档 %s 第 %d 页纸张不匹配!!!\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode, p->offset);
						// 网络模式 [10/17/2014 chenhong]
						if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
						{
							GenLog(ERROR_INFO,"%s[%d].文档纸张不匹配!!!\n", __FILE__, __LINE__);
							status = -1;
							goto Exit;
						}
						CString strPaper;
						strPaper.Format(_T("文档 %s 第 %d 页匹配纸张失败！继续将采用打印机驱动设置纸张，是否继续打印？"), pJobinfo->m_PrintJobInfo.szFileName, p->offset);

						if ((IDOK == ShowMsgBox(strPaper.GetBuffer(0), MB_OKCANCEL)) && (WORKING_NETWORK != HDAppConfig::Instance()->m_ExConfig.m_nWorkingModel))
						{
							papersize = GetPaperSizeByDriver(pJobinfo, devMode, hdcPrint);
							//paperorientation = devMode->dmOrientation;
							if (papersize == 0)
							{
								if (pageNo > pJobinfo->m_JobStatusInfo.m_nStartPage)
								{
									continue;
								}
								else
								{
									status = -1;
									goto Exit;
								}
							}
						}
						else
						{
							if (pageNo > pJobinfo->m_JobStatusInfo.m_nStartPage)
							{
								continue;
							}
							else
							{
								status = -1;
								goto Exit;
							}
						}
					}
				}
				if (((m_nLastPageSize != papersize) || (m_nLastOrientation != paperorientation)) && (PRINTTYPE_PINTYPE != pJobinfo->m_PrintJobInfo.nPrintType))
					//if ((m_nLastPageSize != papersize) || (m_nLastOrientation != paperorientation)) 
				{
					m_nLastPageSize = papersize;
					m_nLastOrientation = paperorientation;

					devMode->dmPaperSize = papersize;
					devMode->dmPaperWidth = 0;
					devMode->dmPaperLength = 0;
					devMode->dmOrientation = paperorientation;
					devMode->dmScale = 100;//pJobinfo->m_JobStatusInfo.m_nPageScaling;

					devMode->dmCollate = DMCOLLATE_TRUE;

					//ResetDC function can be used to change 
					//the paper orientation or paper bins while printing a document
					if(ResetDC(*hdcPrint, devMode))
					{
						GenLog(DEBUG_INFO, "%s[%d].Reset DC success ,Orientation value =%d , papersize = %d \n",__FILE__,__LINE__,devMode->dmOrientation, devMode->dmPaperSize);
					}
					else
					{
						GenLog(ERROR_INFO,"%s[%d].Reset DC failed ,Orientation value =%d , papersize = %d \n",__FILE__,__LINE__,devMode->dmOrientation, devMode->dmPaperSize);
					}

					//设置打印机的默认纸张和方向
					PRINTER_INFO_2 *ppi2 = GetInfo2();
					GenLog(ERROR_INFO,"%s[%d].GetInfo2!!!\n", __FILE__, __LINE__);

					if (ppi2)
					{
						ppi2->pDevMode->dmFields = DM_PAPERSIZE|DM_PAPERWIDTH|DM_PAPERLENGTH|DM_ORIENTATION;
						ppi2->pDevMode->dmPaperSize = papersize;
						ppi2->pDevMode->dmPaperWidth = 0;
						ppi2->pDevMode->dmPaperLength = 0;
						ppi2->pDevMode->dmOrientation = paperorientation;
						if (SetInfo2(ppi2))
						{
							GenLog(DEBUG_INFO, "%s[%d].设置打印机默认纸张成功！\n",__FILE__,__LINE__);
						}
						else
						{
							GenLog(ERROR_INFO, "%s[%d].设置打印机默认纸张失败！\n",__FILE__,__LINE__);
						}

						GlobalFree((HGLOBAL)ppi2);
					}
				}

				//打印内容
				status = PrintOnePage_PDF(pJobinfo, pageNo, hdcPrint, index);
				if(status != 0)
				{
					GenLog(DEBUG_INFO, "%s[%d].文档%s打印第%d份第%d页失败!\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szEventCode, index+1, p->offset);
					goto Exit;
				}
				else
				{
					GenLog(ERROR_INFO, "%s[%d].文档%s打印第%d份第%d页成功!\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szEventCode, index+1, p->offset);
				}
			}
		}

	}
	else if (m_nFileType == BIG_FILETYPE)
	{

		TCHAR szFullPath[MAX_PATH] = {0x00}; //执行文件全路径
		GetModuleFileName(NULL , szFullPath , MAX_PATH );
		(_tcsrchr(szFullPath,_T('\\')))[1] = 0;

		//获取文件名称，截取文件类型
		TCHAR szExt[10] = {0x00};
		TCHAR szFileName[256] = {0x00};
		_tsplitpath(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, NULL, NULL, szFileName, szExt);

		if (strcmp(szExt, ".pdf") == 0  ||
			strcmp(szExt, ".doc") == 0  ||
			strcmp(szExt, ".docx") == 0 ||
			strcmp(szExt, ".wps") == 0  ||
			strcmp(szExt, ".pptx") == 0  ||
			strcmp(szExt, ".ppt") == 0)
		{
			CString csInWordFilePath;
			//CString csInBarCodePath;
			CString csOutPdfFilePath;
			char caInBarCodePath[1024] = {0}; //条码图片路径
			char caInBarCodeDesc[1024] = {0}; //条码描述
			char caInBarcode[1024] = {0}; //条码号
			csInWordFilePath = pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename;
			csOutPdfFilePath = CHDDataCenter::Instance()->GetDirectory(1);
			csOutPdfFilePath = csOutPdfFilePath + szFileName;
			csOutPdfFilePath = csOutPdfFilePath + ".pdf";



			CString csCmdOrder(szFullPath);
			//csCmdOrder = szFullPath;
			csCmdOrder = csCmdOrder + "pdfcreate-new.exe";
			//生成条码图片
			if(0 != CreatBarCodeGraph(pJobinfo , caInBarCodePath))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成图片失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}
			//生成条码描述
			if( 0 != CreatBarCodeDesc(pJobinfo , caInBarCodeDesc,index+1))
			{
				GenLog(DEBUG_INFO, "%s[%d]生成条码描述失败 \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;
			}

			//条码图片路径
			CString csInBarCodePath(caInBarCodePath);
			//条码描述
			CString csInBarCodeDesc(caInBarCodeDesc);

			csCmdOrder = "\"" + csCmdOrder + "\" \"";
			csCmdOrder = csCmdOrder + csInWordFilePath + "\" \"";
			csCmdOrder = csCmdOrder + csInBarCodePath + "\" \"";
			csCmdOrder = csCmdOrder + csOutPdfFilePath + "\" \"";
			csCmdOrder = csCmdOrder + csInBarCodeDesc + "\"";
			//页数
			switch(pJobinfo->m_PrintJobInfo.nPerPage)
			{
			case 1:
				csCmdOrder = csCmdOrder + " Home";
				break;
			case 2:
				csCmdOrder = csCmdOrder + " Trailing";
				break;
			case 3:
				csCmdOrder = csCmdOrder + " All";
				break;
			default:
				GenLog(DEBUG_INFO, "%s[%d]选择页数 error! \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;		
			}

			//位置和大小
			switch(pJobinfo->m_PrintJobInfo.nPosition)
			{
			case 0:
				csCmdOrder = csCmdOrder + " 0.3 0.26 0.330";//右上
				break;
			case 1:
				csCmdOrder = csCmdOrder + " 0.3 0.012 0.330";//左上
				break;;
			case 2:
				csCmdOrder = csCmdOrder + " 0.3 0.25 0.008";//右下
				break;
			case 3:
				csCmdOrder = csCmdOrder + " 0.3 0.012 0.008";//左下
				break;
			case 4:
				csCmdOrder = csCmdOrder + " 0.3 0.14 0.330";
				break;
			case 5:
				csCmdOrder = csCmdOrder + " 0.3 0.14 0.008";
				break;
			default:
				GenLog(DEBUG_INFO, "%s[%d]选择页数 error! \n", __FILE__, __LINE__);
				status = -1;
				goto Exit;		
			}

			GenLog(DEBUG_INFO, "%s[%d]EexShell1  csCmdOrder :[%s] \n", __FILE__, __LINE__ , csCmdOrder.GetBuffer() );

			//执行命令
			char result[1024] = {0};
			if( 0 != ExeShell(csCmdOrder.GetBuffer() , result))
			{
				GenLog(DEBUG_INFO, "%s[%d]EexShell1 error! csCmdOrder :[%s] result :[%s]\n", __FILE__, __LINE__ , csCmdOrder.GetBuffer() ,result );
				status = -1;
				goto Exit;
			}
			//判断文件加条码是否成功
			if(!strstr(result , "SUCC"))
			{
				GenLog(DEBUG_INFO, "%s[%d]EexShell2 error! csCmdOrder :[%s] result :[%s]\n", __FILE__, __LINE__ , csCmdOrder.GetBuffer() ,result);
				status = -1;
				goto Exit;
			}
			csInWordFilePath.ReleaseBuffer();
			csInBarCodePath.ReleaseBuffer();
			csCmdOrder.ReleaseBuffer();

			Sleep(1*1000);	

			int nPrintDouble = pJobinfo->m_PrintJobInfo.nPrintDouble;
			GenLog(DEBUG_INFO, "%s[%d]大文件打印 单双面为: %d \n", __FILE__, __LINE__, nPrintDouble);
			if (nPrintDouble == 1)
			{
				// 还原单面模板 [10/31/2018 Administrator]
				TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
				sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\printone.dat\" u", m_strPrinterPath.GetBuffer(0));
				GenLog(ERROR_INFO, "%s[%d].大文件打印，还原打印机首选项单面面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
				WinExec(szCmdbufferPro,SW_HIDE);
				Sleep(1*1000);
			} 
			else
			{
				// 还原双面面模板 [10/31/2018 Administrator]
				TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
				sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\printdouble.dat\" u", m_strPrinterPath.GetBuffer(0));
				GenLog(ERROR_INFO, "%s[%d].大文件打印，还原打印机首选项双面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
				WinExec(szCmdbufferPro,SW_HIDE);
				Sleep(1*1000);
			}

			// 输出pdf文件
			//ShowMsgBox(_T("提示：PDF大文件打印时，请在本份作业打印出纸全部完毕后，再打印下一份作业。\n如果有Adobe Reader程序启动，请手动关闭，否则会影响台账记录，谢谢合作！"), MB_OKCANCEL);
			CString csPrintPdf;
			int pdfversion = HDAppConfig::Instance()->m_ExConfig.m_pdfversion;
			GenLog(ERROR_INFO, "%s[%d].pdf版本为：pdfversion = %d ，1为PDF10，0为PDF9！\n", __FILE__, __LINE__, pdfversion);
			if (1 == pdfversion)
			{
				csPrintPdf.Format("start acrord32 /p /h \"%s\"", csOutPdfFilePath.GetBuffer());
				GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			} 
			else
			{
				csPrintPdf.Format("start acrobat /p /h \"%s\"", csOutPdfFilePath.GetBuffer());
				GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
			}


			system(csPrintPdf.GetBuffer());
			csPrintPdf.ReleaseBuffer();
			csOutPdfFilePath.ReleaseBuffer();

			Sleep(10000);
			WinExec(_T("taskkill -f -im acrobat.exe -im acrord32.exe"),SW_HIDE);
			Sleep(500);
			////if (nPrintDouble != 1)
			////{
			////	// 还原单面模板 [10/31/2018 Administrator]
			////	TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
			////	sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Sr /n \"%s\" /a \"C:\\Windows\\printone.dat\" u", m_strPrinterPath.GetBuffer(0));
			////	GenLog(ERROR_INFO, "%s[%d].大文件打印，打印完成后，默认还原成单面模板，命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
			////	WinExec(szCmdbufferPro,SW_HIDE);
			////	Sleep(2*1000);
			////}
		}
		else
		{
			ShowMsgBox(_T("系统目前支持 doc/docx/wps/pdf 格式，其他格式文件请勿使用大文件打印的方式，谢谢合作！"), MB_OK);
			status = -1;
			goto Exit;
		}

		////////// 先备份打印机首选项模板 [10/31/2018 Administrator]
		////////TCHAR szCmdbufferPro[MAX_PATH] = {0x00};
		////////sprintf_s(szCmdbufferPro, "rundll32 printui.dll,PrintUIEntry /Ss /n \"%s\" /a \"C:\\Windows\\printdoboule.dat\" u", m_strPrinterPath.GetBuffer(0));
		////////GenLog(ERROR_INFO, "%s[%d].大文件打印，备份打印机首选项单面模板命令为szCmdbufferPro  = %s !\n",__FILE__,__LINE__, szCmdbufferPro);
		////////WinExec(szCmdbufferPro,SW_HIDE);
		////////Sleep(1*1000);

		////////// 大文件打印，先调用打印首选项对话框，配置单双面，否则打印出来的单双面会跟实际不符 [10/30/2018 Administrator]
		////////TCHAR szCmdbuffer[MAX_PATH] = {0x00};
		////////sprintf_s(szCmdbuffer, "rundll32 printui.dll,PrintUIEntry /e /n \"%s\"", m_strPrinterPath.GetBuffer(0));
		////////GenLog(ERROR_INFO, "%s[%d].大文件打印，调用打印机首选项命令为szCmdbuffer  = %s !\n",__FILE__,__LINE__, szCmdbuffer);
		////////WinExec(szCmdbuffer,SW_HIDE);
		////////Sleep(2*1000);
		//////////ShowMsgBox(_T("请根据您的实际需要，配置打印机首选项单双面打印选项。配置完打印首选项单双面之后再关闭此框。谢谢合作！"), MB_OKCANCEL);		
		////////MessageBox(NULL, _T("请根据您的实际需要，配置打印机首选项单双面打印选项。\n配置完打印首选项单双面之后再关闭此框。谢谢合作！"), _T("航盾控制台"), MB_OK);

		//////////////////////////////////////////////////////////////////////
		//20190423 注销代码 
		//TCHAR szExt[10] = {0x00};
		//_tsplitpath(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, NULL, NULL, NULL, szExt);
		//// 判断文件格式

		//if (strcmp(szExt, ".doc") == 0 || strcmp(szExt, ".docx") == 0 || strcmp(szExt, ".wps") == 0)
		//{
		//	//创建word操作对象
		//	HDWordOffice wordOffice;
		//	m_pWordOffice = wordOffice;
		//	m_pWordOffice.CreateApp();
		//	//打开对应word模板文件
		//	//if(OpenWordReceipt(_T("C:\\321.docx")) == -1)
		//	if(OpenWordReceipt(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename) == -1)
		//	{
		//		ShowMsgBox(_T("操作word文件失败，请检查本地是否安装office，谢谢合作！"), MB_OK);
		//		status = -1;
		//		goto Exit;
		//	}
		//	//打印输出
		//	m_pWordOffice.PrintOut();
		//	//释放word
		//	m_pWordOffice.~HDWordOffice();

		//}
		//else if (strcmp(szExt, ".pdf") == 0)
		//{
		//	// 输出pdf文件
		//	ShowMsgBox(_T("提示：PDF大文件打印时，请在本份作业打印出纸全部完毕后，再打印下一份作业。\n如果有Adobe Reader程序启动，请手动关闭，否则会影响台账记录，谢谢合作！"), MB_OKCANCEL);
		//	CString csPrintPdf;
		//	csPrintPdf.Format("start acrord32 /p /h \"%s\"", pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename);
		//	GenLog(DEBUG_INFO, "%s[%d].输出PDF指令:%s!\n",__FILE__,__LINE__, csPrintPdf.GetBuffer());
		//	system(csPrintPdf.GetBuffer());
		//	csPrintPdf.ReleaseBuffer();

		//	Sleep(10000);
		//}
		//else
		//{
		//	ShowMsgBox(_T("系统目前支持 doc/docx/wps/pdf 格式，其他格式文件请勿使用大文件打印的方式，谢谢合作！"), MB_OK);
		//	status = -1;
		//	goto Exit;
		//}
		///////////////////////////////////////////////////////////////// 
	}

	if (m_nFileType == BIG_FILETYPE)
	{
		SetDefaultPrinter(szDefPrinter);
	}
	else if ((m_nFileType == PDF_FILETYPE) && (nFlags == 1))
	{
		//	SetDefaultPrinter(szDefPrinter);
	}
	else
	{
		int endoc = EndDoc (*hdcPrint);
		if (endoc <= 0)
		{
			goto Exit;
		}

	}


Exit:

	m_nLastPageSize = 0;
	m_nLastOrientation = 0;
	p=NULL;
	next=NULL;
	//SetTipBoxProg(100);
	return status;
}

BOOL CHDPrinter::CheckTextContent(CStringArray& strArray, TCHAR* szContent)
{
	if (szContent == NULL)
	{
		return FALSE;
	}

	for (int i = 0; i < strArray.GetCount(); i++)
	{
		CString strTmp = strArray.GetAt(i);
		if (strTmp.CompareNoCase(szContent) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

void CHDPrinter::HDDrawText(const char* mess, HDC hDC, int x, int y ,int fontSize)
{
	//GenLog(DEBUG_INFO,"%s[%d].打印前的条码内容为%s\n",__FILE__,__LINE__, mess);
	float ratio_x = 0;
	float ratio_y = 0;

	LOGFONT   logfont;       //改变输出字体
	HFONT   hFont;

	ratio_x = (float)GetDeviceCaps(hDC, LOGPIXELSX) / 600;
	ratio_y = (float)GetDeviceCaps(hDC, LOGPIXELSY) / 600;
	ZeroMemory(&logfont, sizeof(LOGFONT));   
	logfont.lfCharSet = GB2312_CHARSET;   
	logfont.lfHeight = (int)-fontSize * ratio_x;      //设置字体的大小
	_tcsncpy_s(logfont.lfFaceName, LF_FACESIZE, _T("黑体"), 7); 
	//_tcsncpy_s(logfont.lfFaceName, LF_FACESIZE, _T("黑体"), HDAppConfig::Instance()->m_ExConfig.m_nBarcodePrintFont);
	hFont  =   CreateFontIndirect(&logfont);  
	SelectObject(hDC, hFont); 
	SetTextAlign(hDC, TA_LEFT | TA_TOP);
	TextOut(hDC, x, y, mess, strlen(mess));



	//GenLog(DEBUG_INFO,"%s[%d].打印后的条码内容为%s，错误信息：%s\n",__FILE__,__LINE__, mess, GetErrorMessage());
}
char * GetSID(char *sid)
{
	char userName[260] = "";
	//char sid[260] = "";
	DWORD nameSize = sizeof(userName);
	GetUserName((LPTSTR)userName,&nameSize);
	char userSID[260] = "";
	char userDomain[260] = "";
	DWORD sidSize = sizeof(userSID);
	DWORD domainSize = sizeof(userDomain);
	SID_NAME_USE snu;
	LookupAccountName(NULL,
		(LPTSTR)userName,
		(PSID)userSID,
		&sidSize,
		(LPTSTR)userDomain,
		&domainSize,
		&snu);
	PSID_IDENTIFIER_AUTHORITY psia = GetSidIdentifierAuthority(userSID);
	sidSize = sprintf(sid, "S-%lu-",SID_REVISION);
	sidSize += sprintf(sid +strlen(sid),"%-lu",psia->Value[5]);
	int i = 0;
	int subAuthorities = *GetSidSubAuthorityCount(userSID);
	for (i = 0;i < subAuthorities; i++)
	{
		sidSize += sprintf(sid + sidSize,"-%lu",*GetSidSubAuthority(userSID,i));
	}
	return sid;
}

void CHDPrinter::HDDrawText_201(const char* mess, HDC hDC, int x, int y ,int fontSize)
{
	//GenLog(DEBUG_INFO,"%s[%d].打印前的条码内容为%s\n",__FILE__,__LINE__, mess);
	float ratio_x = 0;
	float ratio_y = 0;

	LOGFONT   logfont;       //改变输出字体
	HFONT   hFont;

	ratio_x = (float)GetDeviceCaps(hDC, LOGPIXELSX) / 600;
	ratio_y = (float)GetDeviceCaps(hDC, LOGPIXELSY) / 600;
	ZeroMemory(&logfont, sizeof(LOGFONT));   
	logfont.lfCharSet = GB2312_CHARSET;   
	logfont.lfHeight = (int)-fontSize * ratio_x;      //设置字体的大小
	_tcsncpy_s(logfont.lfFaceName, LF_FACESIZE, _T("黑体"), 7); 
	logfont.lfEscapement = 900;
	hFont  =   CreateFontIndirect(&logfont);  
	SelectObject(hDC, hFont); 
	SetTextAlign(hDC, TA_LEFT | TA_TOP);
	TextOut(hDC, x, y, mess, strlen(mess));



	//GenLog(DEBUG_INFO,"%s[%d].打印后的条码内容为%s，错误信息：%s\n",__FILE__,__LINE__, mess, GetErrorMessage());
}
int CHDPrinter::GetDIBColorCount(const BITMAPINFOHEADER  bmih)
{
	if ( bmih.biBitCount <= 8 )
		if ( bmih.biClrUsed )
			return bmih.biClrUsed;
		else
			return 1 << bmih.biBitCount;
	else if ( bmih.biCompression==BI_BITFIELDS )
		return 3 + bmih.biClrUsed;
	else
		return bmih.biClrUsed;
}

int CHDPrinter::HDDrawBitmap(char* FileName,HDC hDC, RECT * rect,float& inewx,float& inewy,int type, PrintJob* pJobinfo)
{
	int OldWidth = 0;
	int OldHeigth = 0;
	float zoomSize = 1.0;
	int newx = 0, newy = 0;
	const BYTE* pBits = NULL;
	const BITMAPINFO* pBMI ;
	float ratio_x, ratio_y;

	int printerDpi_X = GetDeviceCaps(hDC, LOGPIXELSX); //获取设备X轴的DPI
	int printerDpi_Y = GetDeviceCaps(hDC, LOGPIXELSY); //获取设备Y轴的DPI

	ratio_x = (float)GetDeviceCaps(hDC, LOGPIXELSX) / 600;
	ratio_y = (float)GetDeviceCaps(hDC, LOGPIXELSY) / 600;

	pBMI = HDLoadBitmap(FileName);

	if(pBMI)
	{
		OldWidth  = pBMI->bmiHeader.biWidth;
		OldHeigth = abs(pBMI->bmiHeader.biHeight);
		GenLog(DEBUG_INFO, "%s[%d].读取条码的长度：%d，读取条码的宽度：%d\n",__FILE__,__LINE__, OldWidth, OldHeigth);
		pBits = (const BYTE *) & pBMI->bmiColors[GetDIBColorCount(pBMI->bmiHeader)];

		int nDestWidth = 0;
		int nDestHeight = 0;
		switch(type)
		{
		case GEN39_CODE://一维码
			nDestWidth = (int)(OldWidth*4*ratio_x);
			nDestHeight = (int)(OldHeigth*4*ratio_y);
			break;

		case QR_CODE://QR
			nDestWidth = (int)(OldWidth*4*ratio_x);
			nDestHeight = (int)(OldHeigth*4*ratio_y);
			break;

		case PDF417_CODE://PDF417
			nDestWidth = (int)(OldWidth*3*ratio_x);
			nDestHeight = (int)(OldHeigth*3*ratio_y);
			break;

		default:
			break;
		}

		//-------------------这里需要通过文档密级获取打印设置，来计算位置
		float fnewx = 0.0;
		float fnewy = 0.0;
		if (!pJobinfo->m_bIsReceipt)
		{
			fnewx = (pJobinfo->m_PrintJobInfo.nCordX /25.39999918) * printerDpi_X;//left
			if ((pJobinfo->m_PrintJobInfo.nPosition == 0) || 
				(pJobinfo->m_PrintJobInfo.nPosition == 2)
				)
			{  //right
				//条码偏移量,以毫米为单位
				float barcodesize_x = 0;
				if(pJobinfo->m_PrintJobInfo.nBarcodeType == GEN39_CODE)
				{
					//一维码
					barcodesize_x = (42 / 25.39999918) * printerDpi_X;
				}
				if(pJobinfo->m_PrintJobInfo.nBarcodeType == QR_CODE)
				{
					//QR 码
					barcodesize_x = (15 / 25.39999918) * printerDpi_X;
				}
				if(pJobinfo->m_PrintJobInfo.nBarcodeType == PDF417_CODE)
				{
					barcodesize_x = (52 / 25.39999918) * printerDpi_X;
				}

				barcodesize_x = nDestWidth;
				//此处的0.2单位是英尺  2.53cm
				fnewx = rect->right - barcodesize_x - (pJobinfo->m_PrintJobInfo.nCordX /25.39999918) * printerDpi_X; 
			}

			fnewy = (pJobinfo->m_PrintJobInfo.nCordY/25.39999918) * printerDpi_Y;//up
			if ((pJobinfo->m_PrintJobInfo.nPosition == 2) || \
				(pJobinfo->m_PrintJobInfo.nPosition == 3) || 
				(pJobinfo->m_PrintJobInfo.nPosition == 5))
			{  //down
				//基于经验值的条码偏移量,以毫米为单位
				float barcodesize_y = 0;
				if(pJobinfo->m_PrintJobInfo.nBarcodeType == GEN39_CODE)
				{
					//一维码
					barcodesize_y = (7/25.39999918) * printerDpi_Y;
				}
				if(pJobinfo->m_PrintJobInfo.nBarcodeType == QR_CODE)
				{
					//QR 码
					barcodesize_y = (15/25.39999918) * printerDpi_Y;
				}
				if(pJobinfo->m_PrintJobInfo.nBarcodeType == PDF417_CODE)
				{
					barcodesize_y = (7 / 25.39999918) * printerDpi_Y;
				}
				barcodesize_y = nDestHeight;
				//此处的0.2单位是英尺  2.53cm
				fnewy = rect->bottom - barcodesize_y - (pJobinfo->m_PrintJobInfo.nCordY /25.39999918) * printerDpi_Y  - 160;//减120是为了给字留空间 
			}		

			if (pJobinfo->m_PrintJobInfo.nPosition == 4 || (pJobinfo->m_PrintJobInfo.nPosition == 5))
			{
				fnewx = rect->right/2 - nDestWidth/2;
			}
			inewx = fnewx;
			inewy = fnewy;
		}
		else
		{
			fnewx = inewx;
			fnewy = inewy;
		}

		if (COMPANY_KEGONGJU == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
		{
			nDestWidth = (int)(OldWidth*HDAppConfig::Instance()->m_ExConfig.nXRatio*ratio_x);
			nDestHeight = (int)(OldHeigth*HDAppConfig::Instance()->m_ExConfig.nYRatio*ratio_y);
			StretchDIBits(hDC, (int)fnewx, (int)fnewy/*0,0*/, nDestWidth, nDestHeight, 0, 0, OldWidth, OldHeigth, 
				pBits, pBMI, DIB_RGB_COLORS, SRCCOPY);	//Gen39
			GenLog(ERROR_INFO,"%s[%d].打印交接单，科工局条码规则!\n",__FILE__,__LINE__);
		}
		else
		{
			StretchDIBits(hDC, (int)fnewx, (int)fnewy/*0,0*/, nDestWidth, nDestHeight, 0, 0, OldWidth, OldHeigth, 
				pBits, pBMI, DIB_RGB_COLORS, SRCCOPY);	//Gen39

		}


		free((void *)pBMI);
	}

	return 0;
}
//生成条码下的描述
int CHDPrinter::CreatBarCodeDesc(PrintJob* pJobinfo ,char* caOutBarCodeDesc,int nPrintCount)
{
	char szTxBuf[1024] = {0x00};
	char tempfiletype[12] = {0x00};
	char temp[6] = {0x00};
	if (pJobinfo->m_bIsReceipt)
	{
		sprintf(szTxBuf,"%s",pJobinfo->m_ReceiptJobInfo.szJobCode);	//通用版本
	}
	else
	{
		CStringArray strArray;
		ParseString(strArray, '|', pJobinfo->m_PrintJobInfo.szTextContent);

		BOOL bFirst = TRUE;
		if(HDAppConfig::Instance()->m_ExConfig.textcontent==1)
		{
			sprintf(szTxBuf, "%s", pJobinfo->m_szFileBarcode);
				//密级
				char szSecLevel[MAX_PATH] = {0x00};
				CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szSecLevel);
				strcat(tempfiletype, szSecLevel);
				strcat(szTxBuf,"-");
				strcat(szTxBuf,tempfiletype);
				//制作方式
				strcat(szTxBuf,"-D");
				//部门代号
				strcat(szTxBuf,"-");				
				strcat(szTxBuf,CHDDataCenter::Instance()->m_CurrentUser.m_szExtCode);
				//日期
				time_t nowtime;
				tm *pNowtime=NULL;
				nowtime = time(NULL);
				pNowtime = localtime(&nowtime);
				char csPrinttime[20] ={0x00};
				sprintf(csPrinttime,"%04d/%02d/%02d",pNowtime->tm_year+1900,pNowtime->tm_mon+1,pNowtime->tm_mday);				
				strcat(szTxBuf,"-");
				strcat(szTxBuf,csPrinttime);
				//流水			
				strcat(szTxBuf,"-");				
				char szcode[32] = {0x00};
				strncpy(szcode,  &pJobinfo->m_szFileBarcode[9],13);
				strcat(szTxBuf,szcode);
				//份号/总份数页数			
				strcat(szTxBuf,"-");
				char csPageCount[20] ={0x00};
				sprintf(csPrinttime,"%d/%d-%d",nPrintCount,pJobinfo->m_PrintJobInfo.nPrintCount,pJobinfo->m_PrintJobInfo.nPageCount);		
				strcat(szTxBuf,csPrinttime);

				GenLog(DEBUG_INFO, "%s[%d].输出字符%s\n",__FILE__, __LINE__,  szTxBuf);
		}
		else if(pJobinfo->m_PrintJobInfo.nBarcodeType)
		{
			// 条码值可配 [3/9/2015 chenhong]
			if (CheckTextContent(strArray, "tm"))	//条码
			{
				sprintf(szTxBuf, "%s", pJobinfo->m_szFileBarcode);	//通用版本
				bFirst = FALSE;
			}
			else
			{
				bFirst = TRUE;
			}

#ifdef CASIC_GROUP
			sprintf(szTxBuf,"");		//集团版本 不打条码
			bFirst = TRUE;
#endif//CASIC_GROUP
			if (CheckTextContent(strArray, "yh"))	//用户
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,(const char*)(pJobinfo->m_PrintJobInfo.szUserName));
			}
			if (CheckTextContent(strArray, "mj"))	//密级
			{
				char szSecLevel[MAX_PATH] = {0x00};
#ifdef HONGYU_JINGGONG
				CString strFileName;
				strFileName.Format(_T("%s"), pJobinfo->m_PrintJobInfo.m_szFileTitle);
				int nRet = strFileName.Find(_T("_HDinjob"));
				if ((nRet >= 0) && (strFileName.GetLength() == (nRet + 8)))
				{
					strcat(szSecLevel, "校对");
				}
				else
				{
					HDIOCP::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szSecLevel);
				}
#else
				CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szSecLevel);
#endif
				strcat(tempfiletype, szSecLevel);

				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,tempfiletype);
			}

			if (CheckTextContent(strArray, "bm"))	//部门
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,pJobinfo->m_PrintJobInfo.szGroupName);
			}

			if (CheckTextContent(strArray, "lx"))	//类型
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"打印");
			}

			if (CheckTextContent(strArray, "rq"))	//日期
			{
				time_t nowtime;
				tm *pNowtime=NULL;
				nowtime = time(NULL);
				pNowtime = localtime(&nowtime);
				char csPrinttime[20] ={0x00};
				// 删除年 [4/3/2015 chenhong]
				//sprintf(csPrinttime,"%02d年%02d月%02d日",pNowtime->tm_year+1900-2000,pNowtime->tm_mon+1,pNowtime->tm_mday);
				sprintf(csPrinttime,"%02d%02d",pNowtime->tm_mon+1,pNowtime->tm_mday);

				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,csPrinttime);
			}
			if (CheckTextContent(strArray, "fs"))	//份数
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,itoa(pJobinfo->m_PrintJobInfo.nPrintCount, temp, 10));
				//strcat(szTxBuf,"份");
				memset(temp,0x00,6);
			}

			if (CheckTextContent(strArray, "lcqx"))	 //大唐-留存期限  [6/12/2016 haojia]
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,itoa(pJobinfo->m_PrintJobInfo.nOperType, temp, 10));
				strcat(szTxBuf,"年");
				memset(temp,0x00,6);
			}
			//8359 定制条码规则根据nOperType判断是否显示归档等条码信息
			if(pJobinfo->m_PrintJobInfo.nOperType==1)
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"部门归档");
			}
			else if(pJobinfo->m_PrintJobInfo.nOperType==2)
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"档案室归档");
			}
			else if(pJobinfo->m_PrintJobInfo.nOperType==3)
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"不归档");
			}
		}
		else{
			memcpy(caOutBarCodeDesc , "None" ,strlen("None"));//此分支未处理。默认为不输出条码描述
		}
	}
	memcpy(caOutBarCodeDesc , szTxBuf ,strlen(szTxBuf));
	return 0;
}
//生成条码图片
int CHDPrinter::CreatBarCodeGraph(PrintJob* pJobinfo ,char *caOutBarCodePath)
{
	TCHAR barcode[MAX_PATH*2] = {0x00};

	//生成条码
	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile,".bmp");
	GenLog(ERROR_INFO,"%s[%d].开始生成条码图片！\n", __FILE__, __LINE__);
	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*2*sizeof(char));
		memcpy(barcode, pJobinfo->m_ReceiptJobInfo.szJobCode, strlen(pJobinfo->m_ReceiptJobInfo.szJobCode));
	}
	if(pJobinfo->m_PrintJobInfo.nBarcodeType == 0 )
	{
		memcpy(caOutBarCodePath , "None" ,strlen("None"));
		GenLog(ERROR_INFO,"%s[%d].无图片！\n", __FILE__, __LINE__);
		return 0;
	}
	else if(pJobinfo->m_PrintJobInfo.nBarcodeType == GEN39_CODE)
	{
		if (!Gen39Code(pJobinfo->m_szFileBarcode, (char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT))
		{
			//失败，如何处理？
			GenLog(ERROR_INFO,"%s[%d].生成一维码%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
			return -1;
		}
	}
	else if(pJobinfo->m_PrintJobInfo.nBarcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		if (!qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode))
		{
			//失败，如何处理？
			GenLog(ERROR_INFO,"%s[%d].生成GR码%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
			return -1;
		}
	}
	else if(pJobinfo->m_PrintJobInfo.nBarcodeType == PDF417_CODE)
	{
		// 生成二维码图片 [1/8/2015 chenhong]
		if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER||m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_Batch)
		{
			GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, m_szBarcode2Code, strlen(m_szBarcode2Code));

			CString strBarcode;
			if((COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)||(COMPANY_CAEP == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)||(COMPANY_307 == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType))
			{
				strBarcode.Format(_T("%s"),pJobinfo->m_szFileBarcode);

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
			}
			else if(COMPANY_716 == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				base64_encode2((unsigned char*)pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode), strBarcode);
				GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
				strBarcode.ReleaseBuffer();

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT, 3, 20, 0))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
			}
			else
			{
				base64_encode2((unsigned char*)m_szBarcode2Code, strlen(m_szBarcode2Code), strBarcode);
				GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
				strBarcode.ReleaseBuffer();

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
			}

			strBarcode.ReleaseBuffer();
		}
		else
		{
			//pdf417调用
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				GenerateCETCBarcode2(pJobinfo, barcode);
				GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
				CString strBarcode;
				base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
				GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
				strBarcode.ReleaseBuffer();

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
				strBarcode.ReleaseBuffer();
			}
			else
			{
				//
			}
		}

	}

	//判断条码图片是否存在
	if(!FileIsExits((char *)m39bmpFile) && ((pJobinfo->m_PrintJobInfo.nBarcodeType == GEN39_CODE) ||\
		(pJobinfo->m_PrintJobInfo.nBarcodeType == QR_CODE) ||(pJobinfo->m_PrintJobInfo.nBarcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在\n",__FILE__, __LINE__, m39bmpFile);
		return -1;
	}
	memcpy(caOutBarCodePath , m39bmpFile ,strlen(m39bmpFile));
	return 0;
}

//barcodeType 条码类型： 0-不出条码，1-文字
int CHDPrinter::AttachBarcode(PrintJob* pJobinfo, HDC *hdcPrint,RECT* rect, int nBarWidth, int nBarHeight, int index, int nPageNo)
{
	TCHAR barcode[MAX_PATH*2] = {0x00};

	if(rect->left<0)
	{
		rect->left = 0;
	}
	if(rect->top<0)
	{
		rect->top = 0;
	}

	GenLog(ERROR_INFO,"%s[%d].开始生成条码图片！\n", __FILE__, __LINE__);
	int printerDpi_X = GetDeviceCaps(*hdcPrint, LOGPIXELSX); //获取设备X轴的DPI
	int printerDpi_Y = GetDeviceCaps(*hdcPrint, LOGPIXELSY); //获取设备Y轴的DPI

	//////////////////////////////////////////////////////////////////////////
	//生成条码
	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile,".bmp");
	GenLog(ERROR_INFO,"%s[%d].开始生成条码图片！\n", __FILE__, __LINE__);
	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*2*sizeof(char));
		memcpy(barcode, pJobinfo->m_ReceiptJobInfo.szJobCode, strlen(pJobinfo->m_ReceiptJobInfo.szJobCode));
	}
	int m_nCreateBarcodeEnCode= m_HDAppConfig->m_ExConfig.m_nCreateBarcodeEnCode; //pdf417是否加密
	if(pJobinfo->m_PrintJobInfo.nBarcodeType == GEN39_CODE)
	{
		if (!Gen39Code(pJobinfo->m_szFileBarcode, (char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT))
		{
			//失败，如何处理？
			GenLog(ERROR_INFO,"%s[%d].生成一维码%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
			return -1;
		}
	}
	else if(pJobinfo->m_PrintJobInfo.nBarcodeType == QR_CODE )
	{
		//QR 码
		QrCode qr;
		if (!qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode))
		{
			//失败，如何处理？
			GenLog(ERROR_INFO,"%s[%d].生成GR码%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
			return -1;
		}
	}
	else if(pJobinfo->m_PrintJobInfo.nBarcodeType ==PDF417_CODE)
	{
		// 生成二维码图片 [1/8/2015 chenhong]
		if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER||m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_Batch)
		{
			GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, m_szBarcode2Code, strlen(m_szBarcode2Code));

			CString strBarcode;
			if((COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)||(COMPANY_CAEP == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)/*||(COMPANY_KEGONGJU == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)*/)
			{
				strBarcode.Format(_T("%s"),m_szBarcode2Code);

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
			}
			else if(COMPANY_716 == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				base64_encode2((unsigned char*)pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode), strBarcode);
				GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
				strBarcode.ReleaseBuffer();

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT, 3, 20, 0))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
			}
			else if(COMPANY_KEGONGJU == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				//strBarcode.Format(_T("%s"),m_szBarcode2Code);
				base64_encode2((unsigned char*)m_szBarcode2Code, strlen(m_szBarcode2Code), strBarcode);
				GenLog(DEBUG_INFO, "%s[%d].加密1前：%s\n",__FILE__,__LINE__, m_szBarcode2Code);
				GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
				strBarcode.ReleaseBuffer();
				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
			}
			else
			{
				if(m_nCreateBarcodeEnCode==1)//pdf417条码608 不加密
				{
					if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, m_szBarcode2Code, PRINT))
					{
						//失败，如何处理？
						GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
						return -1;
					}
				}
				else
				{					
					base64_encode2((unsigned char*)m_szBarcode2Code, strlen(m_szBarcode2Code), strBarcode);
					GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
					strBarcode.ReleaseBuffer();

					if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
					{
						//失败，如何处理？
						GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
						return -1;
					}
				}
			}

			//strBarcode.ReleaseBuffer();
		}
		else
		{
			//pdf417调用
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				GenerateCETCBarcode2(pJobinfo, barcode);
				GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
				CString strBarcode;
				base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
				GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
				strBarcode.ReleaseBuffer();

				if(!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
				{
					//失败，如何处理？
					GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
					return -1;
				}
				strBarcode.ReleaseBuffer();
			}
			else
			{
				if(m_nCreateBarcodeEnCode==1)//pdf417条码608 不加密
				{
					TCHAR szBarcode2[1024] = {0x00};
					GenerateBarcode2(pJobinfo, index, szBarcode2);
					GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, szBarcode2, strlen(szBarcode2));
					if (!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, szBarcode2, PRINT))
					{
						//失败，如何处理？
						GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
						return -1;
					}
				}
				else
				{
					TCHAR szBarcode2[1024] = {0x00};
					GenerateBarcode2(pJobinfo, index, szBarcode2);
					GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, szBarcode2, strlen(szBarcode2));
					CString strBarcode;
					base64_encode2((unsigned char*)szBarcode2, strlen(szBarcode2), strBarcode);
					GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());
					if (!PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT))
					{
						//失败，如何处理？
						GenLog(ERROR_INFO,"%s[%d].生成PDF417%s 失败！\n", __FILE__, __LINE__, m39bmpFile);
						return -1;
					}
				strBarcode.ReleaseBuffer();				}
			}
		}

	}

	//判断条码图片是否存在
	if(!FileIsExits((char *)m39bmpFile) && ((pJobinfo->m_PrintJobInfo.nBarcodeType == GEN39_CODE) ||\
		(pJobinfo->m_PrintJobInfo.nBarcodeType == QR_CODE) ||(pJobinfo->m_PrintJobInfo.nBarcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在\n",__FILE__, __LINE__, m39bmpFile);
		return -1;

		sprintf(m39bmpFile,"%sgougebarcode.bar", m_HDAppConfig->m_szRegPath);
	}

	//////////////////////////////////////////////////////////////////////////
	//开始准备绘制图片
	const BYTE* pBits = NULL;
	const BITMAPINFO* pBMI;

	float ratio_x = (float)GetDeviceCaps(*hdcPrint, LOGPIXELSX) / 600;
	float ratio_y = (float)GetDeviceCaps(*hdcPrint, LOGPIXELSY) / 600;

	//这两个值是为了获取条码下边字的位置
	float fnewx = 0.0;
	float fnewy = 0.0;

	int OldWidth = 0;
	int OldHeigth = 0;
	int nDestWidth = 0;
	int nDestHeight = 0;
	if ((pJobinfo->m_PrintJobInfo.nPosition == 6) || (pJobinfo->m_PrintJobInfo.nPosition == 7))
	{
		// 201条码规则，当条码为竖向左juzhong、右居中时，把生成的PDF条码图片旋转九十度 [4/13/2020 Administrator]
		// 把控制台安装目录下的BMP条码图片旋转九十度 [4/13/2020 Administrator]
		// 读取源条码 [4/13/2020 Administrator]
		readBmp(m39bmpFile);
		//生成旋转九十度后的条码
		char dest_m39bmpFile[MAX_PATH*2] = {0x00};
		strcat(dest_m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
		strcat(dest_m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
		strcat(dest_m39bmpFile,"_dest.bmp");
		GenLog(ERROR_INFO,"%s[%d].旋转九十度后的条码图片路径为：%s！\n", __FILE__, __LINE__,dest_m39bmpFile);
		rotatebmp(dest_m39bmpFile, pBmpBuf, bmpWidth, bmpHeight, biBitCount);//旋转90度
		pBMI = HDLoadBitmap(dest_m39bmpFile);
	} 
	else
	{
		pBMI = HDLoadBitmap(m39bmpFile);
	}
	/*pBMI = HDLoadBitmap(m39bmpFile);*/
	if (pBMI)
	{
		OldWidth  = pBMI->bmiHeader.biWidth;
		OldHeigth = abs(pBMI->bmiHeader.biHeight);
		GenLog(DEBUG_INFO, "%s[%d].读取条码的长度：%d，读取条码的宽度：%d\n",__FILE__,__LINE__, OldWidth, OldHeigth);
		pBits = (const BYTE *) & pBMI->bmiColors[GetDIBColorCount(pBMI->bmiHeader)];

		nDestWidth = (int)(OldWidth*HDAppConfig::Instance()->m_ExConfig.nXRatio*ratio_x);
		nDestHeight = (int)(OldHeigth*HDAppConfig::Instance()->m_ExConfig.nYRatio*ratio_y);
	}

	//计算位置
	fnewx = rect->left+(pJobinfo->m_PrintJobInfo.nCordX /25.39999918) * printerDpi_X;//left
	if ((pJobinfo->m_PrintJobInfo.nPosition == 0) || 
		(pJobinfo->m_PrintJobInfo.nPosition == 2)
		)
	{//right
		fnewx = rect->right - nDestWidth - (pJobinfo->m_PrintJobInfo.nCordX /25.39999918) * printerDpi_X; 
	}

	fnewy = rect->top+(pJobinfo->m_PrintJobInfo.nCordY/25.39999918) * printerDpi_Y;//up
	if ((pJobinfo->m_PrintJobInfo.nPosition == 2) || \
		(pJobinfo->m_PrintJobInfo.nPosition == 3) || 
		(pJobinfo->m_PrintJobInfo.nPosition == 5))
	{//down
		fnewy = rect->bottom - nDestHeight - (pJobinfo->m_PrintJobInfo.nCordY /25.39999918) * printerDpi_Y  - 160;//减120是为了给字留空间 
	}		

	if (pJobinfo->m_PrintJobInfo.nPosition == 4 || (pJobinfo->m_PrintJobInfo.nPosition == 5))
	{//middle
		fnewx = rect->right/2 - nDestWidth/2;
	}
	if (pJobinfo->m_PrintJobInfo.nPosition == 6 || (pJobinfo->m_PrintJobInfo.nPosition == 7))
	{//middle
		fnewy = rect->bottom - nDestHeight - (pJobinfo->m_PrintJobInfo.nCordY /25.39999918) * printerDpi_Y  - 160;
		fnewy = fnewy/2;
	}

	if (pJobinfo->m_PrintJobInfo.nPosition == 7)
	{//middle
		fnewx = rect->right - nDestWidth/2 - 280;
	}
	//绘制
	switch (pJobinfo->m_PrintJobInfo.nBarcodeType)
	{
	case GEN39_CODE:
		{
			StretchDIBits(*hdcPrint, (int)fnewx, (int)fnewy, nDestWidth, nDestHeight, 0, 0, OldWidth, OldHeigth, 
				pBits, pBMI, DIB_RGB_COLORS, SRCCOPY);
			//fnewy += (7.3/25.39999918) * printerDpi_Y;		//Gen39
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				fnewy += (15.5/25.39999918) * printerDpi_Y;		//PDF417 (4.8/25.39999918) * printerDpi_Y + 60;		//PDF417
			}
			else
			{
				fnewy += (10/25.39999918) * printerDpi_Y;		//PDF417
			}
		}
		break;

	case QR_CODE:
		{
			StretchDIBits(*hdcPrint, (int)fnewx, (int)fnewy, nDestWidth, nDestHeight, 0, 0, OldWidth, OldHeigth, 
				pBits, pBMI, DIB_RGB_COLORS, SRCCOPY);
			fnewy += (18/25.39999918) * printerDpi_Y;	//QR

			//在右边
			if ((pJobinfo->m_PrintJobInfo.nPosition == 0) || 
				(pJobinfo->m_PrintJobInfo.nPosition == 2)
				)
			{
				fnewx -= (14/25.39999918)*printerDpi_X;
			}
		}
		break;

	case PDF417_CODE:
		{
			// 增加输出值位置 [7/16/2014 Administrator]
			fnewx += 20.0;
			//SetStretchBltMode(*hdcPrint，HALFTONE);
			GenLog(DEBUG_INFO, "%s[%d].nDestWidth=[%d]，nDestHeight=[%d]\n",__FILE__,__LINE__, nDestWidth, nDestHeight);
			StretchDIBits(*hdcPrint, (int)fnewx, (int)fnewy, nDestWidth, nDestHeight, 0, 0, OldWidth, OldHeigth, 
				pBits, pBMI, DIB_RGB_COLORS, SRCCOPY);
			// 增加输出值位置 [7/16/2014 Administrator]
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				fnewy += (10/25.39999918) * printerDpi_Y + 60;		//PDF417 (4.8/25.39999918) * printerDpi_Y + 60;		//PDF417
			}
			else
			{
				fnewy += (7.3/25.39999918) * printerDpi_Y + 60;		//PDF417
			}
		}
		break;

	default:
		{
			StretchDIBits(*hdcPrint, (int)fnewx, (int)fnewy, nDestWidth, nDestHeight, 0, 0, OldWidth, OldHeigth, 
				pBits, pBMI, DIB_RGB_COLORS, SRCCOPY);
			GenLog(ERROR_INFO, "%s[%d].条码类型配置错误！\n", __FILE__, __LINE__);
		}
		break;
	}
	free((void *)pBMI);

	//////////////////////////////////////////////////////////////////////////
	//out put characters
	char szTxBuf[1024] = {0x00};
	char tempfiletype[12] = {0x00};
	char temp[6] = {0x00};
	if (pJobinfo->m_bIsReceipt)
	{
		sprintf(szTxBuf,"%s",barcode);	//通用版本
	}
	else
	{
		CStringArray strArray;
		ParseString(strArray, '|', pJobinfo->m_PrintJobInfo.szTextContent);
		
		GenLog(ERROR_INFO, "%s[%d].条码配置%s！\n", __FILE__, __LINE__,pJobinfo->m_PrintJobInfo.szTextContent);
		BOOL bFirst = TRUE;
		if(pJobinfo->m_PrintJobInfo.nBarcodeType)
		{
			if(HDAppConfig::Instance()->m_ExConfig.textcontent==0)
			{
			// 条码值可配 [3/9/2015 chenhong]
			if (CheckTextContent(strArray, "tm"))	//条码
			{
				sprintf(szTxBuf, "%s", pJobinfo->m_szFileBarcode);	//通用版本
				bFirst = FALSE;
			}
			else
			{
				bFirst = TRUE;
			}

#ifdef CASIC_GROUP
			sprintf(szTxBuf,"");		//集团版本 不打条码
			bFirst = TRUE;
#endif//CASIC_GROUP
			if (CheckTextContent(strArray, "yh"))	//用户
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,(const char*)(pJobinfo->m_PrintJobInfo.szUserName));
			}
			if (CheckTextContent(strArray, "mj"))	//密级
			{
				char szSecLevel[MAX_PATH] = {0x00};
#ifdef HONGYU_JINGGONG
				CString strFileName;
				strFileName.Format(_T("%s"), pJobinfo->m_PrintJobInfo.m_szFileTitle);
				int nRet = strFileName.Find(_T("_HDinjob"));
				if ((nRet >= 0) && (strFileName.GetLength() == (nRet + 8)))
				{
					strcat(szSecLevel, "校对");
				}
				else
				{
					HDIOCP::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szSecLevel);
				}
#else
				CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szSecLevel);
#endif
				strcat(tempfiletype, szSecLevel);

				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,tempfiletype);
			}
			if (CheckTextContent(strArray, "bmqx"))	 // 中电7所增加保密期限 格式:密级★期限
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,pJobinfo->m_PrintJobInfo.szProjectCode);

				GenLog(ERROR_INFO, "%s[%d].保密期限：%s！\n", __FILE__, __LINE__,pJobinfo->m_PrintJobInfo.szProjectCode);
				GenLog(ERROR_INFO, "%s[%d].保密期限：%s！\n", __FILE__, __LINE__,szTxBuf);
			}
			if (CheckTextContent(strArray, "bm"))	//部门
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,pJobinfo->m_PrintJobInfo.szGroupName);
			}

			if (CheckTextContent(strArray, "lx"))	//类型
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"打印");
			}

			if (CheckTextContent(strArray, "rq"))	//日期
			{
				time_t nowtime;
				tm *pNowtime=NULL;
				nowtime = time(NULL);
				pNowtime = localtime(&nowtime);
				char csPrinttime[20] ={0x00};
				// 删除年 [4/3/2015 chenhong]
				//sprintf(csPrinttime,"%02d年%02d月%02d日",pNowtime->tm_year+1900-2000,pNowtime->tm_mon+1,pNowtime->tm_mday);
				sprintf(csPrinttime,"%02d%02d",pNowtime->tm_mon+1,pNowtime->tm_mday);

				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,csPrinttime);
			}
			if (CheckTextContent(strArray, "ys"))	//页数
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				// 条码下新增输出当前第几页 [1/6/2015 chenhong]
				strcat(szTxBuf,itoa(nPageNo,temp,10));
				memset(temp,0x00,6);
				strcat(szTxBuf,"/");
				int page = pJobinfo->m_JobStatusInfo.m_nEndPage - pJobinfo->m_JobStatusInfo.m_nStartPage + 1;
				strcat(szTxBuf,itoa(page,temp,10));
				//strcat(szTxBuf,"页");
				memset(temp,0x00,6);
			}
			if (CheckTextContent(strArray, "fs"))	//份数
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,itoa(pJobinfo->m_PrintJobInfo.nPrintCount, temp, 10));
				//strcat(szTxBuf,"份");
				memset(temp,0x00,6);
			}

			if (CheckTextContent(strArray, "lcqx"))	 //大唐-留存期限  [6/12/2016 haojia]
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,itoa(pJobinfo->m_PrintJobInfo.nOperType, temp, 10));
				strcat(szTxBuf,"年");
				memset(temp,0x00,6);
			}

			if (CheckTextContent(strArray, "ft"))	 // 中船702新增文件类型字段 [11/25/2020 Administrator]
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				if (strcmp(pJobinfo->m_PrintJobInfo.szProjectCode,"1") == 0)
				{
					strcat(szTxBuf,"临时");
				} 
				else
				{
					strcat(szTxBuf,"正式");
				}
			}
			//8359 定制条码规则
			if(pJobinfo->m_PrintJobInfo.nOperType==1)
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"部门归档");
			}
			else if(pJobinfo->m_PrintJobInfo.nOperType==2)
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"档案室归档");
			}
			else if(pJobinfo->m_PrintJobInfo.nOperType==3)
			{
				if (!bFirst)
				{
					strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
				}
				else
				{
					bFirst = FALSE;
				}
				strcat(szTxBuf,"不归档");
			}
			}
			else
			{//定制条码  条码-密级-制作方式-部门代号-年月日-流水-份号/总份数-总页数
				//条码
				sprintf(szTxBuf, "%s", pJobinfo->m_szFileBarcode);
				//密级
				char szSecLevel[MAX_PATH] = {0x00};
				CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szSecLevel);
				strcat(tempfiletype, szSecLevel);
				strcat(szTxBuf,"-");
				strcat(szTxBuf,tempfiletype);
				//制作方式
				strcat(szTxBuf,"-D");
				//部门代号
				strcat(szTxBuf,"-");				
				strcat(szTxBuf,CHDDataCenter::Instance()->m_CurrentUser.m_szExtCode);
				//日期
				time_t nowtime;
				tm *pNowtime=NULL;
				nowtime = time(NULL);
				pNowtime = localtime(&nowtime);
				char csPrinttime[20] ={0x00};
				sprintf(csPrinttime,"%04d/%02d/%02d",pNowtime->tm_year+1900,pNowtime->tm_mon+1,pNowtime->tm_mday);				
				strcat(szTxBuf,"-");
				strcat(szTxBuf,csPrinttime);
				//流水			
				strcat(szTxBuf,"-");				
				char szcode[32] = {0x00};
				strncpy(szcode,  &pJobinfo->m_szFileBarcode[9],13);
				strcat(szTxBuf,szcode);
				//份号/总份数页数			
				strcat(szTxBuf,"-");
				char csPageCount[20] ={0x00};
				sprintf(csPrinttime,"%d/%d-%d",index+1,pJobinfo->m_PrintJobInfo.nPrintCount,pJobinfo->m_PrintJobInfo.nPageCount);		
				strcat(szTxBuf,csPrinttime);

				GenLog(DEBUG_INFO, "%s[%d].输出字符%s\n",__FILE__, __LINE__,  szTxBuf);
			}
			//if(pJobinfo->m_PrintJobInfo.nSeclvCode==5)
			//{
			//	if (!bFirst)
			//{
			//	strcat(szTxBuf,"-");	//集团不打条码，第一个名字不需要-
			//}
			//else
			//{
			//	bFirst = FALSE;
			//}
			//	strcat(szTxBuf,"此密级文件严禁拍照");
			//}
		
		
			//在右边的话，将文字向左偏移一定位置，以文字长度计量
			if ((pJobinfo->m_PrintJobInfo.nPosition == 0) || 
				(pJobinfo->m_PrintJobInfo.nPosition == 2)
				)
			{
				// 条码居右暂不考虑显示不全 [3/9/2015 chenhong]
				//fnewx -= (strlen(szTxBuf)/25.39999918)/2*printerDpi_X;
			}
			
			//界面设置的偏移量
			fnewx += (m_HDAppConfig->m_AppConfig.m_fBarcodeTextXOffset/25.39999918) * printerDpi_X; 
			fnewy += (m_HDAppConfig->m_AppConfig.m_fBarcodeTextYOffset/25.39999918) * printerDpi_Y;
			int barcode_tips = HDAppConfig::Instance()->m_ExConfig.m_barcode_tips;
			
			if (pJobinfo->m_PrintJobInfo.nPosition == 6)
			{//middle
				fnewx += 280.0;
				fnewy += 1200;
				this->HDDrawText_201(szTxBuf,*hdcPrint,fnewx,fnewy,80);		
			}

			else if (pJobinfo->m_PrintJobInfo.nPosition == 7)
			{//middle
				fnewx = rect->right - nDestWidth/2;
				fnewy += 1200;
				this->HDDrawText_201(szTxBuf,*hdcPrint,fnewx,fnewy,80);		
			}
			else
			{
				//this->HDDrawText(szTxBuf,*hdcPrint,fnewy,fnewx,80);		
				//this->HDDrawText(szTxBuf,*hdcPrint,fnewx,fnewy,80);
				this->HDDrawText(szTxBuf,*hdcPrint,fnewx,fnewy,HDAppConfig::Instance()->m_ExConfig.m_nBarcodePrintFont);//打印的纸张上，条码图片下方文字字体大小
			}
			if((barcode_tips==1)&&(pJobinfo->m_PrintJobInfo.nSeclvCode==5))
			{
				fnewy+=80;
				this->HDDrawText("内部文件妥善保管，严禁拍照，严禁发布至商密网、互联网",*hdcPrint,fnewx,fnewy,HDAppConfig::Instance()->m_ExConfig.m_nBarcodePrintFont+10);//打印的纸张上，条码图片下方文字字体大小
				
			}

			GenLog(DEBUG_INFO, "%s[%d].文档%s输出字符%s\n",__FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode, szTxBuf);
			
		}		
	}

	return 0;
}

//读图像的位图数据、宽、高、颜色表及每像素位数等数据进内存，存放在相应的全局变量中
bool CHDPrinter::rotatebmp(char *bmpName, unsigned char *imgBuf, int width, int height,
	int biBitCount/*,RGBQUAD *pColorTable*/)
{
	if(!imgBuf)
		return 0;
	int colorTablesize=0;//颜色表大小，以字节为单位，灰度图像颜色表为1024字节，彩色图像颜色表大小为0
	int lineByte=(width*biBitCount/8+3)/4*4;          //待存储图像数据每行字节数为4的倍数
	int myLineByte=((height*biBitCount/8+3))/4*4;
	FILE *fp=fopen(bmpName,"wb");//以二进制写的方式打开文件
	if(fp==0) return 0;
	BITMAPFILEHEADER fileHead;   //申请位图文件头结构变量，填写文件头信息
	fileHead.bfType = 0x4D42;    //bmp类型
	//bfSize是图像文件4个组成部分之和
	fileHead.bfSize= sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)
		+ colorTablesize + myLineByte*width;
	fileHead.bfReserved1 = 0;
	fileHead.bfReserved2 = 0;
	fileHead.bfOffBits=54+colorTablesize;//bfOffBits是图像文件前3个部分所需空间之和
	fwrite(&fileHead, sizeof(BITMAPFILEHEADER),1, fp);//写文件头进文件
	//申请位图信息头结构变量，填写信息头信息
	BITMAPINFOHEADER head;
	head.biBitCount=biBitCount;
	head.biClrImportant=0;
	head.biClrUsed=0;
	head.biCompression=0;
	head.biHeight=width;    //高等于宽
	head.biPlanes=1;
	head.biSize=40;
	head.biSizeImage=myLineByte*width;
	head.biWidth=height;   //宽等于高
	head.biXPelsPerMeter=0;
	head.biYPelsPerMeter=0;
	fwrite(&head, sizeof(BITMAPINFOHEADER),1, fp);
	unsigned char* rotateBuf = new unsigned char [width*myLineByte];
	for(int i=0;i<bmpHeight;i++)
	{
		for(int j=0;j<bmpWidth;j++)
		{
			for(int k=0;k<3;k++)
			{
				rotateBuf[(width-1-j)*myLineByte+i*3+k] = pBmpBuf[i*lineByte+j*3+k];
			}
		}
	}
	fwrite(rotateBuf, width*myLineByte, 1, fp);
	fclose(fp);//关闭文件
	return 1;
}
bool CHDPrinter::readBmp(char *bmpName)
{
	FILE *fp=fopen(bmpName,"rb");        //二进制读方式打开指定的图像文件
	if(fp==0) return 0;                  //若打开不成功
	fseek(fp, sizeof(BITMAPFILEHEADER),0);//跳过位图文件头结构BITMAPFILEHEADER
	BITMAPINFOHEADER head;               //定义位图信息头结构变量，读取位图信息头进内存，存放在变量head中
	fread(&head,sizeof(BITMAPINFOHEADER),1,fp); //获取图像宽、高、每像素所占位数等信息
	bmpWidth = head.biWidth;
	bmpHeight = head.biHeight;
	biBitCount = head.biBitCount;
	//定义变量，计算图像每行像素所占的字节数（4的倍数，不到补满）
	int lineByte=(bmpWidth * biBitCount/8+3)/4*4;
	pBmpBuf=new unsigned char[lineByte * bmpHeight]; //开字符数组
	fread(pBmpBuf,1,lineByte * bmpHeight,fp);        //获取图像数据区的信息
	fclose(fp);
	return 1;
}


int CHDPrinter::PrintOnePage(PrintJob* pJobinfo, TAILQ_FileInfo* fileinfo, HDC* hdc, int index)
{
	HENHMETAFILE hemf ;
	RECT rect;
	int printerDpi_X = 600;
	int printerDpi_Y = 600;
	int status = 0;

	char szCopy[256] = {0x00};

	float fnewx = 0.0, fnewy = 0.0;//条码输出的位置，相对于文档的绝对输出范围
	float fdocx = 0.0, fdocy = 0.0;//文档份数的输出位置，相对于文档的绝对输出范围

	printerDpi_X = GetDeviceCaps(*hdc, LOGPIXELSX); //获取设备X轴的DPI
	printerDpi_Y = GetDeviceCaps(*hdc, LOGPIXELSY); //获取设备Y轴的DPI

	//基于经验值的页面偏移量,以毫米为单位
	int iExpOffset_up = GetDeviceCaps(*hdc, PHYSICALOFFSETY) + (m_HDAppConfig->m_AppConfig.m_fPageUpOffset / 25.39999918) * printerDpi_Y;
	int iExpOffset_bottom = GetDeviceCaps(*hdc, PHYSICALOFFSETY) + (m_HDAppConfig->m_AppConfig.m_fPageBottomOffset / 25.39999918) * printerDpi_Y;
	int iExpOffset_left = GetDeviceCaps(*hdc, PHYSICALOFFSETX) + (m_HDAppConfig->m_AppConfig.m_fPageLeftOffset / 25.39999918) * printerDpi_X ;
	int iExpOffset_right = GetDeviceCaps(*hdc, PHYSICALOFFSETX) + (m_HDAppConfig->m_AppConfig.m_fPageRightOffset / 25.39999918) * printerDpi_X ;

	//基于经验值的条码偏移量,以毫米为单位
	int iBarOffset_up = GetDeviceCaps(*hdc, PHYSICALOFFSETY)/* + (4 / 25.39999918) * printerDpi_Y*/;
	int iBarOffset_left = GetDeviceCaps(*hdc, PHYSICALOFFSETX) /*+ (10 / 25.39999918) * printerDpi_X*/;
	//GenLog(ERROR_INFO,"%s[%d].条码偏移量：iBarOffset_up = %d，iBarOffset_left = %d\n",iBarOffset_up,iBarOffset_left);

	//emf文件打印
	hemf = GetEnhMetaFile (fileinfo->filename);

	if(!hemf)
	{
		GenLog(ERROR_INFO,"%s[%d].GetEnhMetaFile()失败，GetLastError=[%d]\n",__FILE__,__LINE__,GetLastError());
		return -1;
	}

	//可能对rect的修改导致页面放大

	//考虑缩放功能，将缩放后的页面放到居中位置
	float fScaling = float(pJobinfo->m_JobStatusInfo.m_nPageScaling - 100)/100;
	float fVerticalOffset = (fScaling)*297/4;//毫米
	float fHorizonOffset = (fScaling)*210/4;//毫米
	int nVerticalOffset = (fVerticalOffset/25.39999918) * printerDpi_Y;//点
	int nHorizonOffset = (fHorizonOffset/25.39999918) * printerDpi_X;//点

	rect.top   = 0 - iExpOffset_up - nVerticalOffset;
	rect.bottom = (297/25.39999918) * printerDpi_Y - iExpOffset_bottom + nVerticalOffset;
	rect.left   = 0 - iExpOffset_left - nHorizonOffset;
	rect.right = (210/25.39999918) * printerDpi_X - iExpOffset_right + nHorizonOffset;

	ENHMETAHEADER Emf_head;
	if(GetEnhMetaFileHeader(hemf,sizeof(Emf_head), (LPENHMETAHEADER)&Emf_head))
	{
		fVerticalOffset = (fScaling)*Emf_head.szlMillimeters.cy/4;
		fHorizonOffset = (fScaling)*Emf_head.szlMillimeters.cx/4;
		nVerticalOffset = (fVerticalOffset/25.39999918) * printerDpi_Y;
		nHorizonOffset = (fHorizonOffset/25.39999918) * printerDpi_X;

		rect.top   = 0 - iExpOffset_up - nVerticalOffset;		//左上角Y轴坐标
		rect.bottom = ((Emf_head.szlMillimeters.cy)/25.39999918) * printerDpi_Y - iExpOffset_bottom + nVerticalOffset;		//右下角Y轴坐标
		rect.left   = 0 - iExpOffset_left - nHorizonOffset;		//左上角X轴坐标
		rect.right = ((Emf_head.szlMillimeters.cx)/25.39999918) * printerDpi_X - iExpOffset_right + nHorizonOffset;		//右下角X轴坐标

		GenLog(ERROR_INFO, "计算得到的RECT:rect.top %d, rect.bottom %d, rect.left %d, rect.right %d\n", rect.top, rect.bottom, rect.left, rect.right);
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].GetEnhMetaFileHeader()失败,GetLastError=[%d]\n",__FILE__,__LINE__,GetLastError());
		return -1;
	}

	RECT rectTrackCard = rect;
	if ((StartPage (*hdc) > 0))
	{
#ifdef CASIC_SANBU
		//三部绘图仪打印图纸跟踪卡
		if (m_HDAppConfig->m_ExConfig.m_nPrinterType == 2)
		{
			if (fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nStartPage)
			{
				if (!this->PrintTrackCard(pJobinfo, *hdc, rectTrackCard))
				{
					DeleteEnhMetaFile (hemf) ;
					hemf = NULL;
					return -1;
				}
				else
				{
					fnewx = rectTrackCard.left + (50/25.39999918) * printerDpi_X;//left

					fnewy = rectTrackCard.top + (52/25.39999918) * printerDpi_Y;//up

					rect.bottom += rectTrackCard.bottom - rectTrackCard.top;
					rect.top += rectTrackCard.bottom - rectTrackCard.top;
				}
			}
		}
#endif
		GenLog(ERROR_INFO, "%s[%d].使用的的RECT:rect.top %d, rect.bottom %d, rect.left %d, rect.right %d\n",__FILE__,__LINE__, rect.top, rect.bottom, rect.left, rect.right);
		if(!PlayEnhMetaFile (*hdc, hemf, &rect))
		{
			GenLog(ERROR_INFO,"%s[%d].play Enhanced MetaFile Failed:%d\n",__FILE__,__LINE__,GetLastError());
			// 返回值不准确暂时先注释 [7/29/2014 chenhong]
			//status = -1;
		} 
		// 拼图打印不输出页码 [1/4/2015 chenhong]
		if (PRINTTYPE_PUZZLE != pJobinfo->m_PrintJobInfo.nPrintType)
		{
			if (status != -1)
			{
				int nBarWidth = 0;
				int nBarHeight = 0;
#ifndef NO_BARCODE			
				if (pJobinfo->m_PrintJobInfo.nBarcodeType)
				{
					if(1 == pJobinfo->m_PrintJobInfo.nPerPage && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nStartPage)
					{  //首页
						AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index);
					}
					else if(pJobinfo->m_PrintJobInfo.nPerPage == 2 && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nEndPage)
					{  //尾页
						AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index, pJobinfo->m_PrintJobInfo.nPageCount);
					}
					else if(pJobinfo->m_PrintJobInfo.nPerPage == 3)
					{	//条码打印页
						AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index, fileinfo->offset);
					}
				}
#endif
				if(pJobinfo->m_PrintJobInfo.nPageNO > 0)//在第一页打印文件是第几份
				{
					fdocx = (rect.right)/4;
					fdocy = rect.bottom - (printerDpi_Y*0.25) - iBarOffset_up;

					memset(szCopy,0x00,256);
					sprintf(szCopy,"第%d页/共%d页-第%d份/共%d份",fileinfo->offset,pJobinfo->m_PrintJobInfo.nPageCount,index+1,pJobinfo->m_PrintJobInfo.nPrintCount);

					if(1== pJobinfo->m_PrintJobInfo.nPageNO && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nStartPage)
					{  //首页
						this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
					}
					else if(pJobinfo->m_PrintJobInfo.nPageNO == 2 && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nEndPage)
					{  //尾页
						this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
					}
					else if(pJobinfo->m_PrintJobInfo.nPageNO == 3)
					{	//条码打印页
						this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
					}
					memset(szCopy,0x00,256);
				}
			}
		}

		if (EndPage (*hdc)  <= 0)
		{
			GenLog(ERROR_INFO,"%s[%d].Call EndPage Error GetLastError:[%d]\n",__FILE__,__LINE__,GetLastError());
			AbortDoc(*hdc);
			status=-1;
		}
	}
	else
	{
		status=-1;
	}

	//删除emf文件，释放指针
	DeleteEnhMetaFile (hemf) ;
	hemf = NULL;
	return status;
}


int CHDPrinter::PrintOnePage_BMP(PrintJob* pJobinfo, TAILQ_FileInfo* fileinfo, HDC* hdc, int index)
{
	RECT rect;
	int printerDpi_X = 600;
	int printerDpi_Y = 600;
	int status = 0;

	HDC hdcbmp;
	hdcbmp = CreateCompatibleDC(*hdc);

	BITMAP bmpinfo;
	CBitmap* bitmap = new CBitmap();
	HBITMAP maskBMP = NULL;     

	maskBMP = (HBITMAP)LoadImage(NULL, fileinfo->filename, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE);

	SelectObject(hdcbmp, maskBMP); 
	GetObject(maskBMP, sizeof(bmpinfo), &bmpinfo);

	float fnewx = 0.0, fnewy = 0.0;//条码输出的位置，相对于文档的绝对输出范围
	float fdocx = 0.0, fdocy = 0.0;//文档份数的输出位置，相对于文档的绝对输出范围

	printerDpi_X = GetDeviceCaps(*hdc, LOGPIXELSX); //获取设备X轴的DPI
	printerDpi_Y = GetDeviceCaps(*hdc, LOGPIXELSY); //获取设备Y轴的DPI

	//modify by zbin 20190522
	//如果偏移量等没有发生改变直接使用上一次值
	//基于经验值的页面偏移量,以毫米为单位
	int iExpOffset_up = GetDeviceCaps(*hdc, PHYSICALOFFSETY) + (m_HDAppConfig->m_AppConfig.m_fPageUpOffset / 25.39999918) * printerDpi_Y;
	int iExpOffset_bottom = GetDeviceCaps(*hdc, PHYSICALOFFSETY) + (m_HDAppConfig->m_AppConfig.m_fPageBottomOffset / 25.39999918) * printerDpi_Y;
	int iExpOffset_left = GetDeviceCaps(*hdc, PHYSICALOFFSETX) + (m_HDAppConfig->m_AppConfig.m_fPageLeftOffset / 25.39999918) * printerDpi_X ;
	int iExpOffset_right = GetDeviceCaps(*hdc, PHYSICALOFFSETX) + (m_HDAppConfig->m_AppConfig.m_fPageRightOffset / 25.39999918) * printerDpi_X ;

	//基于经验值的条码偏移量,以毫米为单位
	int iBarOffset_up = GetDeviceCaps(*hdc, PHYSICALOFFSETY)/* + (4 / 25.39999918) * printerDpi_Y*/;
	int iBarOffset_left = GetDeviceCaps(*hdc, PHYSICALOFFSETX) /*+ (10 / 25.39999918) * printerDpi_X*/;

	//考虑缩放功能，将缩放后的页面放到居中位置
	float fScaling = float(pJobinfo->m_JobStatusInfo.m_nPageScaling - 100)/100;
	float fVerticalOffset = (fScaling)*297/4;//毫米
	float fHorizonOffset = (fScaling)*210/4;//毫米
	int nVerticalOffset = (fVerticalOffset/25.39999918) * printerDpi_Y;//点
	int nHorizonOffset = (fHorizonOffset/25.39999918) * printerDpi_X;//点

	rect.top   = 0 - iExpOffset_up - nVerticalOffset;
	rect.bottom = bmpinfo.bmHeight*printerDpi_Y/300 - iExpOffset_bottom + nVerticalOffset;
	rect.left   = 0 - iExpOffset_left - nHorizonOffset;
	rect.right = bmpinfo.bmWidth*printerDpi_X/300 - iExpOffset_right + nHorizonOffset;

	StartPage(*hdc);  
	StretchBlt(*hdc, rect.left, rect.top, rect.right, rect.bottom, hdcbmp, 0, 0, bmpinfo.bmWidth, bmpinfo.bmHeight, SRCCOPY);
	char szCopy[256] = {0x00};
	RECT rectTrackCard = rect;
	if (PRINTTYPE_PUZZLE != pJobinfo->m_PrintJobInfo.nPrintType)
	{
		if (status != -1)
		{
			int nBarWidth = 0;
			int nBarHeight = 0;

#ifndef NO_BARCODE			
			if (pJobinfo->m_PrintJobInfo.nBarcodeType)
			{
				if(1 == pJobinfo->m_PrintJobInfo.nPerPage && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nStartPage)
				{  
					//首页
					AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index);
				}
				else if(pJobinfo->m_PrintJobInfo.nPerPage == 2 && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nEndPage)
				{  
					//尾页
					AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index, pJobinfo->m_PrintJobInfo.nPageCount);
				}
				else if(pJobinfo->m_PrintJobInfo.nPerPage == 3)
				{	
					//条码打印页
					AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index, fileinfo->offset);
				}
			}
#endif
			if(pJobinfo->m_PrintJobInfo.nPageNO > 0)//在第一页打印文件是第几份
			{
				fdocx = (rect.right)/4;
				fdocy = rect.bottom - (printerDpi_Y*0.25) - iBarOffset_up;

				memset(szCopy,0x00,256);
				sprintf(szCopy,"第%d页/共%d页-第%d份/共%d份",fileinfo->offset,pJobinfo->m_PrintJobInfo.nPageCount,index+1,pJobinfo->m_PrintJobInfo.nPrintCount);

				if(1== pJobinfo->m_PrintJobInfo.nPageNO && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nStartPage)
				{  
					//首页
					this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
				}
				else if(pJobinfo->m_PrintJobInfo.nPageNO == 2 && fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nEndPage)
				{  
					//尾页
					this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
				}
				else if(pJobinfo->m_PrintJobInfo.nPageNO == 3)
				{	
					//条码打印页
					this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
				}
				memset(szCopy, 0x00, 256);
			}
		}
	}
	DeleteObject(maskBMP);

	EndPage(*hdc);   

	status = 0;
	return status;
}



int CHDPrinter::PrintOnePage_PDF(PrintJob* pJobinfo, int pageNo, HDC* hdc, int index)
{
	static int printerDpi_X = 0;
	static int printerDpi_Y = 0;
	static int iExpOffset_up = 0, iExpOffset_bottom = 0;
	static int iExpOffset_left = 0, iExpOffset_right = 0;
	static SizeI paperSize;
	static RectI printable;
	static float dpiFactor = 0.0f;
	static bool bPrintPortrait = false;
	static float fVerticalOffset = 0.0f, fHorizonOffset = 0.0f;
	static int nVerticalOffset = 0, nHorizonOffset = 0;
	int status = 0;

	char szCopy[256] = {0x00};

	float fnewx = 0.0, fnewy = 0.0;//条码输出的位置，相对于文档的绝对输出范围
	float fdocx = 0.0, fdocy = 0.0;//文档份数的输出位置，相对于文档的绝对输出范围
	// 只在第一次打印时获取设备信息
	if (index==0) {
		printerDpi_X = GetDeviceCaps(*hdc, LOGPIXELSX);
		printerDpi_Y = GetDeviceCaps(*hdc, LOGPIXELSY);

		// 基于经验值的页面偏移量
		iExpOffset_up = (m_HDAppConfig->m_AppConfig.m_fPageUpOffset / 25.39999918) * printerDpi_Y;
		iExpOffset_bottom = (m_HDAppConfig->m_AppConfig.m_fPageBottomOffset / 25.39999918) * printerDpi_Y;
		iExpOffset_left = (m_HDAppConfig->m_AppConfig.m_fPageLeftOffset / 25.39999918) * printerDpi_X;
		iExpOffset_right = (m_HDAppConfig->m_AppConfig.m_fPageRightOffset / 25.39999918) * printerDpi_X;

		paperSize = SizeI(GetDeviceCaps(*hdc, PHYSICALWIDTH), GetDeviceCaps(*hdc, PHYSICALHEIGHT));
		printable = RectI(GetDeviceCaps(*hdc, PHYSICALOFFSETX), GetDeviceCaps(*hdc, PHYSICALOFFSETY),
			GetDeviceCaps(*hdc, HORZRES), GetDeviceCaps(*hdc, VERTRES));

		// 注意：GetFileDPI() 需要在 m_pEngine 有效时获取
		// 如果 m_pEngine 是固定的，也可以缓存
		dpiFactor = (std::min)(GetDeviceCaps(*hdc, LOGPIXELSX) / m_pEngine->GetFileDPI(),
			GetDeviceCaps(*hdc, LOGPIXELSY) / m_pEngine->GetFileDPI());
		bPrintPortrait = paperSize.dx < paperSize.dy;

		// 缩放相关的偏移量计算
		float fScaling = float(pJobinfo->m_JobStatusInfo.m_nPageScaling - 100)/100;
		fVerticalOffset = (fScaling) * paperSize.dy / 4;
		fHorizonOffset = (fScaling) * paperSize.dx / 4;
		nVerticalOffset = (fVerticalOffset / 25.39999918) * printerDpi_Y;
		nHorizonOffset = (fHorizonOffset / 25.39999918) * printerDpi_X;

	}
	printerDpi_X = GetDeviceCaps(*hdc, LOGPIXELSX); //获取设备X轴的DPI
	printerDpi_Y = GetDeviceCaps(*hdc, LOGPIXELSY); //获取设备Y轴的DPI

	if ((StartPage (*hdc) > 0))
	{
		geomutil::SizeT<float> pSize = m_pEngine->PageMediabox(pageNo).Size().Convert<float>();
		int rotation = 0;
		// Turn the document by 90 deg if it isn't in portrait mode
		if (pSize.dx > pSize.dy) {
			rotation += 90;
			std::swap(pSize.dx, pSize.dy);
		}
		// make sure not to print upside-down
		rotation = (rotation % 180) == 0 ? 0 : 270;
		// finally turn the page by (another) 90 deg in landscape mode
		if (!bPrintPortrait) {
			rotation = (rotation + 90) % 360;
			std::swap(pSize.dx, pSize.dy);
		}

		// dpiFactor means no physical zoom
		float zoom = dpiFactor;

		PointI offset(-printable.x, -printable.y);

		//if (pd.advData.scale != PrintScaleNone) {
		// make sure to fit all content into the printable area when scaling
		// and the whole document page on the physical paper
		RectD rect = m_pEngine->PageContentBox(pageNo, Target_Print);
		RectD recd = m_pEngine->Transform(rect, pageNo, 1.0, rotation,FALSE);
		geomutil::RectT<float> cbox = recd.Convert<float>();
		zoom = std::min((float)printable.dx / cbox.dx,
			std::min((float)printable.dy / cbox.dy,
			std::min((float)paperSize.dx / pSize.dx,
			(float)paperSize.dy / pSize.dy)));
		// use the correct zoom values, if the page fits otherwise
		// and the user didn't ask for anything else (default setting)
		//if (PrintScaleShrink == pd.advData.scale && dpiFactor < zoom)
		//zoom = dpiFactor;
		// center the page on the physical paper
		offset.x += (int)(paperSize.dx - pSize.dx * zoom) / 2;
		offset.y += (int)(paperSize.dy - pSize.dy * zoom) / 2;
		// make sure that no content lies in the non-printable paper margins
		geomutil::RectT<float> onPaper(printable.x + offset.x + cbox.x * zoom,
			printable.y + offset.y + cbox.y * zoom,
			cbox.dx * zoom, cbox.dy * zoom);
		if (onPaper.x < printable.x)
			offset.x += (int)(printable.x - onPaper.x);
		else if (onPaper.BR().x > printable.BR().x)
			offset.x -= (int)(onPaper.BR().x - printable.BR().x);
		if (onPaper.y < printable.y)
			offset.y += (int)(printable.y - onPaper.y);
		else if (onPaper.BR().y > printable.BR().y)
			offset.y -= (int)(onPaper.BR().y - printable.BR().y);
		//}
		bool ok = false;
		RectI rc = RectI::FromXY(offset.x - iExpOffset_left - nHorizonOffset , offset.y - iExpOffset_up - nVerticalOffset, paperSize.dx + iExpOffset_right + nHorizonOffset, paperSize.dy + iExpOffset_bottom + nVerticalOffset);	

		RECT rectTrackCard ;
		rectTrackCard.top= rc.y;
		rectTrackCard.bottom= printable.dy + iExpOffset_bottom + nVerticalOffset;
		rectTrackCard.left= rc.x;
		rectTrackCard.right= printable.dx + iExpOffset_right + nHorizonOffset;
		GenLog(ERROR_INFO, "%s[%d].pdf使用的的RECT:rect.top %d, rect.bottom %d, rect.left %d, rect.right %d\n",__FILE__,__LINE__, rc.y, rc.dy+rc.y, rc.x, rc.dx+rc.x);
		GenLog(ERROR_INFO, "%s[%d].条码使用的的RECT:rectTrackCard.top %d, rectTrackCard.bottom %d, rectTrackCard.left %d, rectTrackCard.right %d\n",__FILE__,__LINE__, rectTrackCard.top, rectTrackCard.bottom,rectTrackCard.left, rectTrackCard.right);
		/////////////////////////////////////////////////////////
#ifdef CASIC_SANBU
		//三部绘图仪打印图纸跟踪卡
		if (m_HDAppConfig->m_ExConfig.m_nPrinterType == 2)
		{
			if (fileinfo->offset == pJobinfo->m_JobStatusInfo.m_nStartPage)
			{
				if (!this->PrintTrackCard(pJobinfo, *hdc, rectTrackCard))
				{
					return -1;
				}
				else
				{
					fnewx = rectTrackCard.left + (50/25.39999918) * printerDpi_X;//left

					fnewy = rectTrackCard.top + (52/25.39999918) * printerDpi_Y;//up

					rect.bottom += rectTrackCard.bottom - rectTrackCard.top;
					rect.top += rectTrackCard.bottom - rectTrackCard.top;
				}
			}
		}
#endif
		//////////////////////////////////////////
		/*ok =1;*/
		GenLog(ERROR_INFO,"%s[%d].RenderPageA:%d\n",__FILE__,__LINE__);
		ok = m_pEngine->RenderPageA(*hdc, rc, pageNo, zoom, rotation, NULL, Target_Print);
		GenLog(ERROR_INFO,"%s[%d].RenderPageA:%d\n",__FILE__,__LINE__);
		if(!ok)
		{
			GenLog(ERROR_INFO,"%s[%d].render PDF File Failed:%d\n",__FILE__,__LINE__,GetLastError());
		}		

		// 拼图打印不输出页码 [1/4/2015 chenhong]
		if (PRINTTYPE_PUZZLE != pJobinfo->m_PrintJobInfo.nPrintType)
		{
			if (status != -1)
			{
				int nBarWidth = 0;
				int nBarHeight = 0;
#ifndef NO_BARCODE			
				if (pJobinfo->m_PrintJobInfo.nBarcodeType)
				{
					if(1 == pJobinfo->m_PrintJobInfo.nPerPage && pageNo == pJobinfo->m_JobStatusInfo.m_nStartPage)
					{  //首页
						AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index);
					}
					else if(pJobinfo->m_PrintJobInfo.nPerPage == 2 && pageNo == pJobinfo->m_JobStatusInfo.m_nEndPage)
					{  //尾页
						AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index, pJobinfo->m_PrintJobInfo.nPageCount);
					}
					else if(pJobinfo->m_PrintJobInfo.nPerPage == 3)
					{	//条码打印页
						AttachBarcode(pJobinfo, hdc, &rectTrackCard, nBarWidth, nBarHeight, index, pageNo);
					}
				}
#endif
				if(pJobinfo->m_PrintJobInfo.nPageNO > 0)//在第一页打印文件是第几份
				{
					fdocx = (printable.dy+ iExpOffset_bottom + nVerticalOffset)/4;
					fdocy = printable.dy+ iExpOffset_bottom + nVerticalOffset - (printerDpi_Y*0.25);
					memset(szCopy,0x00,256);
					sprintf(szCopy,"第%d页/共%d页-第%d份/共%d份",pageNo,pJobinfo->m_PrintJobInfo.nPageCount,index+1,pJobinfo->m_PrintJobInfo.nPrintCount);

					if(1== pJobinfo->m_PrintJobInfo.nPageNO && pageNo == pJobinfo->m_JobStatusInfo.m_nStartPage)
					{  //首页
						this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
					}
					else if(pJobinfo->m_PrintJobInfo.nPageNO == 2 && pageNo == pJobinfo->m_JobStatusInfo.m_nEndPage)
					{  //尾页
						this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
					}
					else if(pJobinfo->m_PrintJobInfo.nPageNO == 3)
					{	//条码打印页
						this->HDDrawText(szCopy, *hdc, fdocx, fdocy,80);
					}
					memset(szCopy,0x00,256);
				}
			}
		}

		if (EndPage (*hdc)  <= 0)
		{
			GenLog(ERROR_INFO,"%s[%d].Call EndPage Error GetLastError:[%d]\n",__FILE__,__LINE__,GetLastError());
			AbortDoc(*hdc);
			status=-1;
		}
	}
	else
	{
		status=-1;
	}

	return status;
}


int CHDPrinter::GetQueue(HANDLE hPrinter,JOB_INFO_2 **ppJobInfo,int *pcJobs,DWORD *pStatus)
{
	DWORD				cByteNeeded, nReturned, cByteUsed;
	JOB_INFO_2          *pJobStorage = NULL;
	PRINTER_INFO_2      *pPrinterInfo = NULL;
	if (!GetPrinter(hPrinter, 2, NULL, 0, &cByteNeeded))
	{
		DWORD dwErrorCode = GetLastError();
		if (dwErrorCode != ERROR_INSUFFICIENT_BUFFER)
			return FALSE;
	}

	pPrinterInfo = (PRINTER_INFO_2 *)malloc(cByteNeeded);
	if (!(pPrinterInfo))
		return FALSE;

	if (!GetPrinter(hPrinter,2,(LPBYTE)pPrinterInfo,cByteNeeded,&cByteUsed))
	{
		free(pPrinterInfo);
		pPrinterInfo = NULL;
		return FALSE;
	}

	if (!EnumJobs(hPrinter,0,pPrinterInfo->cJobs,2,NULL,0,(LPDWORD)&cByteNeeded,(LPDWORD)&nReturned))
	{
		if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
		{
			free(pPrinterInfo);
			pPrinterInfo = NULL;
			return FALSE;
		}
	}

	pJobStorage = (JOB_INFO_2 *)malloc(cByteNeeded);
	if (!pJobStorage)
	{

		free(pPrinterInfo);
		pPrinterInfo = NULL;
		return FALSE;
	}
	ZeroMemory(pJobStorage, cByteNeeded);

	if (!EnumJobs(	hPrinter,
		0,
		pPrinterInfo->cJobs,
		2,
		(LPBYTE)pJobStorage,
		cByteNeeded,
		(LPDWORD)&cByteUsed,
		(LPDWORD)&nReturned))
	{
		free(pPrinterInfo);
		free(pJobStorage);
		pJobStorage = NULL;
		pPrinterInfo = NULL;
		return FALSE;
	}
	*pcJobs = nReturned;
	*pStatus = pPrinterInfo->Status;
	*ppJobInfo = pJobStorage;
	free(pPrinterInfo);

	return TRUE;
}
//
//判断系统是否为64位操作系统，是返回TRUE，否则返回FALSE
BOOL  CHDPrinter::IsWow64() 
{ 
	typedef BOOL (WINAPI *LPFN_ISWOW64PROCESS) (HANDLE, PBOOL); 
	LPFN_ISWOW64PROCESS fnIsWow64Process; 
	BOOL bIsWow64 = FALSE; 
	fnIsWow64Process = (LPFN_ISWOW64PROCESS)GetProcAddress(GetModuleHandle("kernel32"), "IsWow64Process"); 
	if (NULL != fnIsWow64Process) 
	{ 
		fnIsWow64Process(GetCurrentProcess(), &bIsWow64);
	} 
	return bIsWow64; 
}

//重启监控模块接口
BOOL  CHDPrinter::KillAndRestartInj()
{
	//如果structSize过大 则重启监控模块
	//Sleep(1*1000);
	//重启进程
	//获取监控模块路径
	TCHAR szFullPath[MAX_PATH] = {0x00}; //执行文件全路径

	GetModuleFileName(NULL , szFullPath , MAX_PATH );
	(_tcsrchr(szFullPath,_T('\\')))[1] = 0;
	//
	CString strPath = szFullPath;
	if(IsWow64())
		strPath =  strPath + "HDInjdlls\\x64\\";
	else
		strPath =  strPath + "HDInjdlls\\x86\\";
	//
	CString strFullPath = strPath + "HDInjdlls.exe";
	//创建进程
	BOOL bRet = TRUE;
	DWORD dwExitCode;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	bRet = CreateProcess(NULL, strFullPath.GetBuffer(0), NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL, 
		&si,
		&pi );
	if( bRet ) //做什么用?
	{													// 关闭子进程的主线程句柄		
		//WaitForSingleObject(pi.hProcess, INFINITE);		// 等待子进程的退出							
		GetExitCodeProcess(&pi.hProcess, &dwExitCode);	// 获取子进程的退出码

		GenLog(DEBUG_INFO,"%s[%d].HDInjdlls.exe重启成功！\n",__FILE__,__LINE__);
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].HDInjdlls.exe重启失败！\n", __FILE__, __LINE__);
	} 
	CloseHandle(pi.hThread);		
	CloseHandle(pi.hProcess); 


	//重启进程
	//获取监控模块路径
	
	 strPath = szFullPath;
	if(IsWow64())
		strPath =  strPath + "HDInjdlls\\x86\\";
	else
		return TRUE;
	//
	 strFullPath = strPath + "HDInjdlls.exe";
	//创建进程
	
	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	bRet = CreateProcess(NULL, strFullPath.GetBuffer(0), NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL, 
		&si,
		&pi );
	if( bRet ) //做什么用?
	{													// 关闭子进程的主线程句柄		
		//WaitForSingleObject(pi.hProcess, INFINITE);		// 等待子进程的退出							
		GetExitCodeProcess(&pi.hProcess, &dwExitCode);	// 获取子进程的退出码

		GenLog(DEBUG_INFO,"%s[%d].HDInjdlls.exe重启成功！\n",__FILE__,__LINE__);
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].HDInjdlls.exe重启失败！\n", __FILE__, __LINE__);
	} 
	CloseHandle(pi.hThread);		
	CloseHandle(pi.hProcess); 
	return TRUE;
}


//************************************
// Method:    SetPrinterParam
// FullName:  CHDPrinter::SetPrinterParam
// Access:    private 
// Returns:   BOOL
// Qualifier: 针对一个打印任务设置整体的打印机参数，包括色彩、方向、纸张（以任务的第一页为准）等，但不涉及具体每页的参数设置，每页的参数设置在
// setinfo2函数中实现。 日期：20140816补充，党伟、石春刚。
// Parameter: PrintJob * pJobinfo
// Parameter: HDC & hdcPrint
// Parameter: LPDEVMODE & devMode
//************************************
BOOL CHDPrinter::SetPrinterParam(PrintJob* pJobinfo, HDC &hdcPrint, LPDEVMODE &devMode)
{
	BOOL bRet = FALSE;

	TCHAR		devstring[256] = {0x00};
	int			status = 0;
	HANDLE      printer = NULL;
	LPBYTE      pPrinter = NULL;
	DWORD       structSize = 0;
	DWORD		returnCode = 0/*,pPrinterSize*/;
	char		*driver = NULL;
	char		*port = NULL;
	int			k = 0;
	//WinExec(_T("taskkill -f -im HDinjdlls.exe -im HDinjdlls.exe -im HDinjdlls64.exe  -im HDinjdlls32.exe -im injdlls.exe syssrv.exe"),SW_HIDE);

	// 特殊打印机设置翻页 [9/22/2014 chenhong]
	//设置特殊打印机普通打印模式下单双面，例如TOSHIBA e-STUDIO系列
	//if (m_strPrinterPath.Find("TOSHIBA e-STUDIO") >= 0)
	//{
	//	GenLog(ERROR_INFO, "%s[%d].使用TOSHIBA e-STUDIO系列打印机：%s\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0));
	//	if (!RegSetDuplex(pJobinfo->m_PrintJobInfo.nPrintDouble))
	//	{
	//		GenLog(ERROR_INFO,"%s[%d].文档%s设置TOSHIBA e-STUDIO系列打印机单双面失败！\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode);
	//		bRet = FALSE;
	//		goto SETPARAM_EXIT;
	//	}
	//}
	if (HDAppConfig::Instance()->m_ExConfig.m_strDuplexPrinter.Find(m_strPrinterPath.GetBuffer(0)) >= 0)
	{
		GenLog(ERROR_INFO, "%s[%d].特殊打印机设置翻页：[%s]\n", __FILE__, __LINE__, m_strPrinterName.GetBuffer(0));
		if (!RegSetDuplex(pJobinfo))
		{
			GenLog(ERROR_INFO,"%s[%d].文档%s设置打印机单双面失败！\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode);
			bRet = FALSE;
			goto SETPARAM_EXIT;
		}
	}
	// End [9/22/2014 chenhong]

	//由于后边用到了port，所以这里顺带检查一下是否存在打印机
	GetProfileString("Devices", m_strPrinterPath, "", devstring, sizeof(devstring));
	driver = strtok ((char*)devstring, (const char *)",");
	port = strtok(NULL, (const char *) ",");

	GenLog(DEBUG_INFO, "%s[%d].driver:%s port:%s\n",__FILE__,__LINE__,driver ,port);

	if (!driver || !port) //检测打印机是否存在
	{		
		GenLog(ERROR_INFO,"%s[%d].文档%s解析打印机失败!\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode);
		bRet = FALSE;
		goto SETPARAM_EXIT;
	}

	PRINTER_DEFAULTS pDefault;
	pDefault.DesiredAccess = PRINTER_ALL_ACCESS;
	pDefault.pDatatype = NULL;
	pDefault.pDevMode = NULL;



	if (!OpenPrinter((LPSTR)m_strPrinterPath.GetBuffer(0), &printer, &pDefault)) 
	{
		GenLog(ERROR_INFO,"%s[%d].文档%s打开打印机%s失败%d\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode, m_strPrinterName,GetLastError());
		bRet = FALSE;
		goto SETPARAM_EXIT;
	}

	GenLog(DEBUG_INFO, "%s[%d].打印机%s打开成功！\n",__FILE__, __LINE__, m_strPrinterPath.GetBuffer(0));

	/*****************************************************20140816dangwei scg xiugai**************************/
	LPDEVMODE pDevModeDW = NULL;
	//	LPDEVMODE pDevModeDW_new = NULL;
	//取LPDEVMODE的大小
	structSize = DocumentProperties(NULL,
		printer,					/* Handle to our printer. */ 
		(LPSTR) m_strPrinterPath.GetBuffer(0),    /* Name of the printer. */ 
		NULL,						/* Asking for size, so */ 
		NULL,						/* these are not used. */ 
		0);							/* Zero returns buffer size. */ 

	GenLog(ERROR_INFO,"%s[%d].调用DocumentProperties函数返回值为structSize = %ld ,GetLastError = %d\n",__FILE__,__LINE__, structSize , GetLastError());
	if(structSize > 0)
	{
		structdevmodeSize=structSize;
		pDevModeDW = (LPDEVMODE)malloc(structSize);
		//pDevModeDW_new = (LPDEVMODE)malloc(structSize);

		GenLog(ERROR_INFO,"%s[%d].调用DocumentProperties函数返回值为structSize = %ld \n",__FILE__,__LINE__, structdevmodeSize);
	}
	if (!pDevModeDW) 
	{
		GenLog(ERROR_INFO, "%s[%d].error:%d\n",__FILE__,__LINE__, GetLastError());
		//重启监控模块
		//BOOL bResult =  KillAndRestartInj();

		//重新获取打印机缓存
		structSize = DocumentProperties(NULL,
			printer,					/* Handle to our printer. */ 
			(LPSTR) m_strPrinterPath.GetBuffer(0),    /* Name of the printer. */ 
			NULL,						/* Asking for size, so */ 
			NULL,						/* these are not used. */ 
			0);
		if(structSize > 0)
			pDevModeDW = (LPDEVMODE)malloc(structSize);
		if (!pDevModeDW)
		{
			GenLog(ERROR_INFO, "%s[%d].文档 devMode分配内存错误,调用历史大小%d;error:%d\n",__FILE__,__LINE__,structdevmodeSize,GetLastError());
			if (structdevmodeSize<0)
			{				
				bRet = FALSE;
				goto SETPARAM_EXIT;
			}
			pDevModeDW = (LPDEVMODE)malloc(structdevmodeSize);

		}	
	}

	//取打印机原始设置，防止丢失
	returnCode = DocumentProperties(NULL,
		printer,
		(LPSTR) m_strPrinterPath.GetBuffer(0),
		pDevModeDW,					/* The address of the buffer to fill. */ 
		NULL,						/* Not using the input buffer. */ 
		/*DM_IN_PROMPT |*/DM_OUT_BUFFER);				/* Have the output buffer filled. */ 
	GenLog(ERROR_INFO, "%s[%d].error:%d\n",__FILE__,__LINE__, GetLastError());
	
	if(pDevModeDW->dmFields&DM_ORIENTATION)//如果打印机支持设置方向，才设置方向
	{
		pDevModeDW->dmOrientation = pJobinfo->m_PrintJobInfo.nPrintDouble;//1是纵向 2是横向
	}
	
	if(pDevModeDW->dmFields&DM_COLOR)//如果打印机支持设置彩色，才设置彩色
	{
		pDevModeDW->dmColor = pJobinfo->m_PrintJobInfo.nColor;		//1是单色 2是彩色
	}
	GenLog(ERROR_INFO, "%s[%d].error:%d\n",__FILE__,__LINE__, GetLastError());
	//若不需要加条码,则份数为提交的份数
	pDevModeDW->dmCopies= 1;				//份数

	int pagesizeNum = GetPaperSizebyName(pJobinfo->m_PrintJobInfo.nPrintType,NULL, pJobinfo->m_PrintJobInfo.szPageSize, NULL);
	GenLog(DEBUG_INFO,"%s[%d].纸张名称为%s!\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szPageSize);

	if(pagesizeNum <= 118 || PRINTTYPE_PINTYPE == pJobinfo->m_PrintJobInfo.nPrintType)
	{
		if(pagesizeNum <= 0) //dangwei
		{
			GenLog(DEBUG_INFO,"%s[%d] 获取到的纸张大小转换为数字时返回一个非正数，存在纸张不匹配的风险!\n",__FILE__,__LINE__);
		}
		pDevModeDW->dmPaperSize = pagesizeNum;
	} 
	//pDevModeDW->dmPaperSize = pagesizeNum;

	pDevModeDW->dmCollate = pJobinfo->m_JobStatusInfo.m_nCollate;
	//devMode->dmFields |= DM_SCALE | DM_PAPERSIZE | DM_DUPLEX |DM_ORIENTATION |DM_COLOR |DM_COPIES |DM_PAPERLENGTH |DM_PAPERWIDTH |DM_PRINTQUALITY |DM_YRESOLUTION|DM_COLLATE;

	// 获取第一张EMF大小 [8/28/2014 chenhong]
	if ((FALSE == pJobinfo->m_bIsReceipt) && (PRINTTYPE_PINTYPE != pJobinfo->m_PrintJobInfo.nPrintType))
	{
		int tmpWidth = 0;
		int tmpHeight = 0;
		if(m_nFileType == EMF_FILETYPE)
		{
			HENHMETAFILE hemf ; 
			int papersize = 0;
			struct TAILQ_FileInfo *p;
			p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList);
			hemf = GetEnhMetaFile (p->filename);
			ENHMETAHEADER Emf_head;
			if(GetEnhMetaFileHeader(hemf, sizeof(Emf_head), (LPENHMETAHEADER)&Emf_head))
			{
				// 如果x>y 为横向，否则为纵向 [8/28/2014 chenhong]
				if (Emf_head.szlMillimeters.cx > Emf_head.szlMillimeters.cy)
				{
					tmpWidth = Emf_head.szlMillimeters.cy;
					tmpHeight = Emf_head.szlMillimeters.cx;
					pDevModeDW->dmOrientation = 2;
				}
				else
				{
					tmpWidth = Emf_head.szlMillimeters.cx;
					tmpHeight = Emf_head.szlMillimeters.cy;
					pDevModeDW->dmOrientation = 1;
				}

				pDevModeDW->dmPaperLength = tmpHeight;
				pDevModeDW->dmPaperWidth = tmpWidth;
				GenLog(DEBUG_INFO, "%s[%d].第一个EMF 文件宽和高分别为%d %d, 横纵向值[%d]！\n",__FILE__,__LINE__, tmpWidth, tmpHeight, pDevModeDW->dmOrientation);
			}
			//删除emf文件，释放指针
			DeleteEnhMetaFile (hemf) ;
			hemf = NULL;
		}
		else if (m_nFileType == BMP_FILETYPE)
		{
			struct TAILQ_FileInfo *p;
			p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList);
			BITMAP bmpinfo;
			HBITMAP maskBMP = NULL;
			maskBMP = (HBITMAP)LoadImage(NULL, p->filename, IMAGE_BITMAP, 0, 0, LR_LOADFROMFILE | LR_CREATEDIBSECTION | LR_DEFAULTSIZE);     
			GetObject(maskBMP, sizeof(bmpinfo), &bmpinfo);

			// 如果x>y 为横向，否则为纵向 [8/28/2014 chenhong]
			if (bmpinfo.bmWidth > bmpinfo.bmHeight)
			{
				tmpWidth = bmpinfo.bmHeight*25.4/300;
				tmpHeight = bmpinfo.bmWidth*25.4/300;
				pDevModeDW->dmOrientation = 2;
			}
			else
			{
				tmpWidth = bmpinfo.bmWidth*25.4/300;
				tmpHeight = bmpinfo.bmHeight*25.4/300;
				pDevModeDW->dmOrientation = 1;
			}
			pDevModeDW->dmPaperLength = tmpHeight;
			pDevModeDW->dmPaperWidth = tmpWidth;
			GenLog(DEBUG_INFO, "%s[%d].第一个EMF 文件宽和高分别为%d %d, 横纵向值[%d]！\n",__FILE__,__LINE__, tmpWidth, tmpHeight, pDevModeDW->dmOrientation);

			DeleteObject(maskBMP);
		}
		else if(m_nFileType == PDF_FILETYPE)
		{
			geomutil::SizeT<float> pSize;
			pSize = m_pEngine->PageMediabox(1).Size().Convert<float>();
			//RectD rect = m_pEngine->PageContentBox(pageNo_PDF, Target_Print);
			const float dpiFactor = (m_pEngine->GetFileDPI()/25.39999918);
			if (pSize.dx > pSize.dy)  //核实单元尺寸
			{
				tmpWidth = pSize.dy/dpiFactor;
				tmpHeight = pSize.dx/dpiFactor;
				pDevModeDW->dmOrientation = 2;
			}
			else
			{
				tmpWidth = pSize.dx/dpiFactor;
				tmpHeight = pSize.dy/dpiFactor;
				pDevModeDW->dmOrientation = 1;
			}
			pDevModeDW->dmPaperLength = tmpHeight;
			pDevModeDW->dmPaperWidth = tmpWidth;
			GenLog(DEBUG_INFO, "%s[%d].第一页PDF文件宽和高分别为%d %d, 横纵向值[%d]！\n",__FILE__,__LINE__, tmpWidth, tmpHeight, pDevModeDW->dmOrientation);
		}
	}
	/*
		// add by zkx 20240612 (35suo针式打印 横纵向设置

		if( PRINTTYPE_PINTYPE == pJobinfo->m_PrintJobInfo.nPrintType)//针式打印
				{

					GenLog(DEBUG_INFO, "%s[%d].nPrintType == 4，修改前第一页宽和高分别为%d, %d,横纵向值[%d]！\n",__FILE__,__LINE__,pDevModeDW->dmPaperWidth, pDevModeDW->dmPaperLength, pDevModeDW->dmOrientation);
					if(pDevModeDW->dmOrientation ==1)
					{
					    pDevModeDW->dmOrientation =2;
					}
					else 
					{
						 pDevModeDW->dmOrientation =1;
					}
					int tempchange=pDevModeDW->dmPaperLength;
					pDevModeDW->dmPaperLength=pDevModeDW->dmPaperWidth;
					pDevModeDW->dmPaperWidth=tempchange;
					GenLog(DEBUG_INFO, "%s[%d].nPrintType == 4，修改前第一页宽和高分别为%d, %d,横纵向值[%d]！\n",__FILE__,__LINE__,pDevModeDW->dmPaperWidth, pDevModeDW->dmPaperLength, pDevModeDW->dmOrientation);

				}
	*/
	//单页或者双页长边翻页、短边翻页

	GenLog(DEBUG_INFO, "%s[%d].打印机翻页设置nPrintDouble：%d,dmFields:%08x\n", __FILE__, __LINE__,pJobinfo->m_PrintJobInfo.nPrintDouble,pDevModeDW->dmFields);
	//if((pDevModeDW->dmFields&DM_DUPLEX)&&(!pJobinfo->m_bIsReceipt))//如果打印机支持设置双面，才设置
	if(!pJobinfo->m_bIsReceipt)//如果打印机支持设置双面，才设置
	{
		if(1 == pJobinfo->m_PrintJobInfo.nPrintDouble)
			pDevModeDW->dmDuplex = DMDUP_SIMPLEX;
		else if(2 == pJobinfo->m_PrintJobInfo.nPrintDouble)
		{
			if (pDevModeDW->dmOrientation == 1)//纵向
			{
				pDevModeDW->dmDuplex = DMDUP_VERTICAL;//2左右翻页
			}
			else//横向
			{
				pDevModeDW->dmDuplex = DMDUP_HORIZONTAL;//3上下翻页
			}
		}
		else
		{
			if (pDevModeDW->dmOrientation == 1)
			{
				pDevModeDW->dmDuplex = DMDUP_HORIZONTAL;
			}
			else
			{
				pDevModeDW->dmDuplex = DMDUP_VERTICAL;
			}
		}
	}
	else
	{
		pDevModeDW->dmDuplex = DMDUP_SIMPLEX;
	}

	GenLog(DEBUG_INFO, "%s[%d].打印机翻页设置nPrintDouble：%d,dmFields:%08x\n", __FILE__, __LINE__,pJobinfo->m_PrintJobInfo.nPrintDouble,pDevModeDW->dmFields);
	if(4==m_HDAppConfig->m_ExConfig.m_nPrinterType)
	{
		GenLog(DEBUG_INFO, "%s[%d].针式打印机启动默认参数%d\n", __FILE__, __LINE__,m_HDAppConfig->m_ExConfig.m_nPrinterType);
		devMode->dmPaperSize=139;
		devMode->dmPaperLength=1395;
		devMode->dmPaperWidth=2160;
		devMode->dmPosition.x=9109505;
		devMode->dmPosition.y=141559155;
		memcpy(devMode->dmFormName,"1/2 Letter",strlen("1/2 Letter"));

		returnCode = DocumentProperties(NULL,
			printer,
			(LPSTR) m_strPrinterPath.GetBuffer(0),
			NULL,
			devMode,
			DM_IN_BUFFER );


	}	
	else
	{
		returnCode = DocumentProperties(NULL,
			printer,
			(LPSTR) m_strPrinterPath.GetBuffer(0),
			devMode,
			pDevModeDW,
			DM_IN_BUFFER | DM_OUT_BUFFER);
	}


	GenLog(DEBUG_INFO, "%s[%d].打印机翻页设置nPrintDouble：%d,dmFields:%08x\n", __FILE__, __LINE__,pJobinfo->m_PrintJobInfo.nPrintDouble,pDevModeDW->dmFields);
	GenLog(DEBUG_INFO, "%s[%d].打印机翻页设置nPrintDouble：%d,dmFields:%08x\n", __FILE__, __LINE__,pJobinfo->m_PrintJobInfo.nPrintDouble,devMode->dmFields);
	if(pDevModeDW->dmPaperSize != devMode->dmPaperSize)
	{
		GenLog(DEBUG_INFO,"%s[%d].针对文档整体的纸张类型设置失败，会导致A3打成A4等问题\n",__FILE__,__LINE__);
	}

	//前提认定文档整体的纸张大小、方向与文档第一页的大小、方向属性一致
	m_nLastPageSize = devMode->dmPaperSize;
	m_nLastOrientation = devMode->dmOrientation;

	/*****************************************************20140816dangwei scg xiugai**************************/

	GenLog(DEBUG_INFO, "%s[%d].文件类型%d进入高级打印选项\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.nPrintType);
	//一次输出日志
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmFormName：%s\n", __FILE__, __LINE__, devMode->dmFormName);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperLength：%d\n", __FILE__, __LINE__, devMode->dmPaperLength);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperWidth：%d\n", __FILE__, __LINE__, devMode->dmPaperWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperSize：%d\n", __FILE__, __LINE__, devMode->dmPaperSize);
	GenLog(DEBUG_INFO, "%s[%d].输出前pDevModeDW->dmFormName：%s\n", __FILE__, __LINE__, pDevModeDW->dmFormName);
	GenLog(DEBUG_INFO, "%s[%d].输出前pDevModeDW->dmPaperLength：%d\n", __FILE__, __LINE__, pDevModeDW->dmPaperLength);
	GenLog(DEBUG_INFO, "%s[%d].输出前pDevModeDW->dmPaperWidth：%d\n", __FILE__, __LINE__, pDevModeDW->dmPaperWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前pDevModeDW->dmPaperSize：%d\n", __FILE__, __LINE__, pDevModeDW->dmPaperSize);
	
	
	if((pJobinfo->m_JobStatusInfo.m_nIsCallDriver) || ( PRINTTYPE_PINTYPE == pJobinfo->m_PrintJobInfo.nPrintType))//针式打印/* || (m_HDAppConfig->m_ExConfig.m_nPrinterType == 2)*/)//针对配置特殊打印机，如绘图仪
	{
		GenLog(DEBUG_INFO, "%s[%d].文件%s进入高级打印选项\n", __FILE__, __LINE__, pJobinfo->m_szFileBarcode);
		ShowTipMsg(_T("请勿在高级模式中重新设置打印份数！"), c_btnDelayTime);
		returnCode = DocumentProperties(NULL,
			printer,
			(LPSTR) m_strPrinterPath.GetBuffer(0),
			devMode,									/* The address of the buffer to fill. */ 
			NULL,										/* Not using the input buffer. */ 
			DM_IN_PROMPT | DM_OUT_BUFFER);				/* Have the output buffer filled. */ 

		if (returnCode != IDOK) 
		{
			GenLog(ERROR_INFO,"%s[%d].文档%s高级打印选项中获取打印机属性失败!\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode);
			bRet = FALSE;
			goto SETPARAM_EXIT;
		}
		Sleep(250);
		// 防止在高级模式中重置打印份数导致台账不准 [8/29/2014 chenhong]
		devMode->dmCopies = 1;
	}
	

	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmSpecVersion：%d\n", __FILE__, __LINE__, devMode->dmSpecVersion);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDriverVersion：%d\n", __FILE__, __LINE__, devMode->dmDriverVersion);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmSize：%d\n", __FILE__, __LINE__, devMode->dmSize);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDriverExtra：%d\n", __FILE__, __LINE__, devMode->dmDriverExtra);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmFields：%d\n", __FILE__, __LINE__, devMode->dmFields);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmOrientation：%d\n", __FILE__, __LINE__, devMode->dmOrientation);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperSize：%d\n", __FILE__, __LINE__, devMode->dmPaperSize);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperLength：%d\n", __FILE__, __LINE__, devMode->dmPaperLength);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperWidth：%d\n", __FILE__, __LINE__, devMode->dmPaperWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmScale：%d\n", __FILE__, __LINE__, devMode->dmScale);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmCopies：%d\n", __FILE__, __LINE__, devMode->dmCopies);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDefaultSource：%d\n", __FILE__, __LINE__, devMode->dmDefaultSource);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPrintQuality：%d\n", __FILE__, __LINE__, devMode->dmPrintQuality);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPosition：%d\n", __FILE__, __LINE__, devMode->dmPosition.x);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPosition：%d\n", __FILE__, __LINE__, devMode->dmPosition.y);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayOrientation：%d\n", __FILE__, __LINE__, devMode->dmDisplayOrientation);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayFixedOutput：%d\n", __FILE__, __LINE__, devMode->dmDisplayFixedOutput);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmColor：%d\n", __FILE__, __LINE__, devMode->dmColor);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDuplex：%d\n", __FILE__, __LINE__, devMode->dmDuplex);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmYResolution：%d\n", __FILE__, __LINE__, devMode->dmYResolution);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmTTOption：%d\n", __FILE__, __LINE__, devMode->dmTTOption);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmCollate：%d\n", __FILE__, __LINE__, devMode->dmCollate);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmFormName：%s\n", __FILE__, __LINE__, devMode->dmFormName);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmLogPixels：%d\n", __FILE__, __LINE__, devMode->dmLogPixels);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmBitsPerPel：%d\n", __FILE__, __LINE__, devMode->dmBitsPerPel);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPelsWidth：%d\n", __FILE__, __LINE__, devMode->dmPelsWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPelsHeight：%d\n", __FILE__, __LINE__, devMode->dmPelsHeight);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayFlags：%d\n", __FILE__, __LINE__, devMode->dmDisplayFlags);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmNup：%d\n", __FILE__, __LINE__, devMode->dmNup);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayFrequency：%d\n", __FILE__, __LINE__, devMode->dmDisplayFrequency);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmICMMethod：%d\n", __FILE__, __LINE__, devMode->dmICMMethod);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmMediaType：%d\n", __FILE__, __LINE__, devMode->dmMediaType);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDitherType：%d\n", __FILE__, __LINE__, devMode->dmDitherType);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmReserved1：%d\n", __FILE__, __LINE__, devMode->dmReserved1);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmReserved2：%d\n", __FILE__, __LINE__, devMode->dmReserved2);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPanningWidth：%d\n", __FILE__, __LINE__, devMode->dmPanningWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPanningHeight：%d\n", __FILE__, __LINE__, devMode->dmPanningHeight);



	if (!ClosePrinter(printer))
	{
		GenLog(ERROR_INFO,"%s[%d].文档%sClosePrinter失败!\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode);
		bRet = FALSE;
		goto SETPARAM_EXIT;
	}

	GenLog(DEBUG_INFO, "%s[%d].ClosePrinter\n", __FILE__, __LINE__);
	hdcPrint = CreateDC(driver, m_strPrinterPath, port, devMode); 

	GenLog(DEBUG_INFO, "%s[%d].CreateDC\n", __FILE__, __LINE__);
	if (!hdcPrint) 
	{
		GenLog(ERROR_INFO,"%s[%d].文档%s打印机初始化失败！\n",__FILE__,__LINE__, pJobinfo->m_szFileBarcode);
	}
	else
	{
		bRet = TRUE;

	}

	GenLog(DEBUG_INFO, "%s[%d].free pDevModeDW\n", __FILE__, __LINE__);
	if(pDevModeDW)
	{
		free(pDevModeDW);
	}

	GenLog(DEBUG_INFO, "%s[%d].SetPrinterParam out \n", __FILE__, __LINE__);
	//BOOL bResult =  KillAndRestartInj();
SETPARAM_EXIT:
	return bRet;
}


DWORD WINAPI CHDPrinter::PrintThread(LPVOID lpParam)
{
	CHDPrinter* pHDPrinter = (CHDPrinter*)lpParam;
	HDC	hdcPrint = NULL;
	PRINTER_INFO_2 *ppi2 = NULL;

	while (TRUE)
	{
		//GenLog(ERROR_INFO,"%s[%d].打印机%s的打印线程进入循环阶段！\n",__FILE__,__LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
		DWORD dwRet = WaitForSingleObject(pHDPrinter->m_hPrintStopEvent, 500);//是否等待，影响速度
		switch (dwRet )
		{
		case WAIT_OBJECT_0:

			GenLog(ERROR_INFO,"%s[%d].打印机%s的打印线程退出！\n",__FILE__,__LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
			ResetEvent(pHDPrinter->m_hPrintStopEvent);
			SetEvent(pHDPrinter->m_hPrintStoppedEvent);
			return 0;
			break;

		default:
			//打印部分
			if ((pHDPrinter->GetUnSendJobCount() != 0) && pHDPrinter->IsSendNext())
			{
				int page = 0;
				GenLog(ERROR_INFO,"%s[%d].打印线程进入打印阶段！\n",__FILE__,__LINE__);
				
				GenLog(ERROR_INFO,"%s[%d].打印机%s的打印线程进入打印阶段！\n",__FILE__,__LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
				//任务没有发送到打印机的标志，FALSE时是打印失败
				BOOL bFailedFlag = TRUE;

				//获取打印任务
				PrintJob* pJobinfo = new PrintJob();
				CopyPrintJob(pJobinfo, pHDPrinter->FindUnSendJob());

				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
				{
					if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
					{
						// 【刷卡器显示】 [9/6/2015 haojia]
						TCHAR cShowMsg[MAX_PATH] = {0x00};
						int nLen = 0;
						sprintf(cShowMsg,_T("开始打印%s"),pJobinfo->m_PrintJobInfo.szFileName);
						nLen = strlen(cShowMsg);
						CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
					}
					if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 2) //十三所定制
					{
						// 【刷卡器显示】 [9/6/2015 haojia]
						TCHAR cShowMsg[MAX_PATH] = {0x00};
						int nLen = 0;
						sprintf(cShowMsg,_T("%s"),_T("开始打印作业..."));
						nLen = strlen(cShowMsg);
						CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
					}
				}

				pHDPrinter->m_csSendedLock.Lock();
				pHDPrinter->m_listSendedJob.InsertAt(pHDPrinter->m_listSendedJob.GetCount(), pJobinfo);
				pHDPrinter->m_csSendedLock.Unlock();

				//置打印任务状态
				CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_JOB_PRINTTING, NULL, (LPARAM)pJobinfo->m_PrintJobInfo.szEventCode);
				pJobinfo->m_JobStatusInfo.m_nStaus = PRINTTING;

				GenLog(DEBUG_INFO, "%s[%d].开始打印文件:%s\n", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szEventCode);

				//先是下载解密解压任务，初始化文件列表
				if (!pJobinfo->m_bIsReceipt)
				{
					if (!pHDPrinter->DownloadAndParse(pJobinfo))
					{
						GenLog(DEBUG_INFO, "%s[%d].文件DownloadAndParse失败\n", __FILE__, __LINE__);
						goto SENDTO_PRINTER_ERROR;
					}
					else
					{
						if(pHDPrinter->m_nFileType == PDF_FILETYPE)
						{
							if(pHDPrinter->m_pEngine)
							{
								delete pHDPrinter->m_pEngine;
								pHDPrinter->m_pEngine = NULL;
							}
							struct TAILQ_FileInfo *p;
							p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList);
							int numsize = strlen(p->filename);
							WCHAR *filename=new WCHAR[numsize+1];
							memset(filename,0x00,(numsize+1)*sizeof(WCHAR));
							MultiByteToWideChar(CP_ACP,0,p->filename,-1,filename,numsize);
							pHDPrinter->m_pEngine = PdfEngine::CreateFromFile(filename);
							if(pHDPrinter->m_pEngine == nullptr)
							{
								GenLog(ERROR_INFO,"%s[%d].PdfEngine::CreateFromFile失败\n",__FILE__,__LINE__);
								goto SENDTO_PRINTER_ERROR;
							}
							delete filename;
						}
					}
				} 
				else
				{
					HDIOCP::Instance()->GetMediaNum(&pJobinfo->m_ReceiptJobInfo);
					DWORD dwStatus = WaitForSingleObject(HDIOCP::Instance()->m_hMediaReceivedEvent, 10*1000);
					if (dwStatus != WAIT_OBJECT_0)
					{
						ResetEvent(HDIOCP::Instance()->m_hMediaReceivedEvent);
						GenLog(DEBUG_INFO, "%s[%d].m_hMediaReceivedEvent事件等待超时失败\n", __FILE__, __LINE__);
						goto SENDTO_PRINTER_ERROR;
					}
					ResetEvent(HDIOCP::Instance()->m_hMediaReceivedEvent);
				}
				GenLog(DEBUG_INFO, "%s[%d].[==========================开始SetPrinterParam============================]\n", __FILE__, __LINE__);
				//设置打印机环境
				ppi2 = pHDPrinter->GetInfo2();
				LPDEVMODE devMode = ppi2->pDevMode;
				if (!pHDPrinter->SetPrinterParam(pJobinfo, hdcPrint, devMode))
				{
					GenLog(DEBUG_INFO, "%s[%d].SetPrinterParam失败\n", __FILE__, __LINE__);
					goto SENDTO_PRINTER_ERROR;
				}
				GenLog(DEBUG_INFO, "%s[%d].[==========================结束SetPrinterParam============================]\n", __FILE__, __LINE__);
				if (pHDPrinter->CheckPrinterStretchDibSupport(hdcPrint))
				{
					// Play the EMF to the printer
					SetCursor (LoadCursor(NULL, IDC_WAIT)) ;
					ShowCursor (TRUE);

					if (pJobinfo->m_bIsReceipt)
					{
						if(HDAppConfig::Instance()->m_ExConfig.m_nRecriptType == 0)
							pHDPrinter->PrintReceipt(pJobinfo, devMode, &hdcPrint);//emf格式
						else
							pHDPrinter->PrintWordReceipt(pJobinfo, devMode, &hdcPrint);//word格式

						//新方法20140504
						PrinttingJob* pPrinttingJob = new PrinttingJob();
						strcpy(pPrinttingJob->szPrintJobID, pJobinfo->m_PrintJobInfo.szEventCode);
						strcpy(pPrinttingJob->szBarcode, pJobinfo->m_szFileBarcode);
						GenLog(DEBUG_INFO, "%s[%d].(PrinttingJob结构体)pPrinttingJob->szPrintJobID：%s\n",__FILE__,__LINE__,  pPrinttingJob->szPrintJobID);
						GenLog(DEBUG_INFO, "%s[%d].(PrinttingJob结构体)pPrinttingJob->szBarcode：%s\n",__FILE__,__LINE__,  pPrinttingJob->szBarcode);
						pHDPrinter->m_csSendedLock.Lock();
						pHDPrinter->m_listSendedInfo.InsertAt(pHDPrinter->m_listSendedInfo.GetCount(), pPrinttingJob);
						pHDPrinter->m_csSendedLock.Unlock();

						memset(pJobinfo->m_szFileBarcode, 0x00, sizeof(char)*c_nChar64);
						CHDDataCenter::Instance()->ClearMediaList();
					}
					else
					{
						GenLog(DEBUG_INFO, "%s[%d].文件%s打印份数[%d]\n",__FILE__,__LINE__,  pJobinfo->m_PrintJobInfo.szFileName, pJobinfo->m_PrintJobInfo.nPrintCount);

						for(int k = 0; k < pJobinfo->m_PrintJobInfo.nPrintCount; k++)
						{
							
						GenLog(DEBUG_INFO, "%s[%d].文件%s打印份数[%d]\n",__FILE__,__LINE__,  pJobinfo->m_PrintJobInfo.szFileName, k);
							/*if(NprinterNum>0)
								{
								k--;
								Sleep(5*1000);
								continue;
							}*/
							
						GenLog(DEBUG_INFO, "%s[%d].文件%s打印份数[%d]\n",__FILE__,__LINE__,  pJobinfo->m_PrintJobInfo.szFileName, k);
							if (pHDPrinter->IsSendNext())//判断是否可以继续发送
							{
							
								WinExec(_T("taskkill -f -im SetPrinter.exe -im syssrv.exe"),SW_HIDE);								
								SetDefaultPrinter(pHDPrinter->m_strPrinterName.GetBuffer(0));
								if(pHDPrinter->PrintOneDoc(pJobinfo, devMode, &hdcPrint, k,&page) != 0)
								{
									//如果不是第一份报错，则有些已经打印出来了，不能从头开始打
									//否则认为没有发送到打印机
									// 网络模式 [10/14/2014 chenhong]
									if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
									{
										GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
										CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
									}
									if (k != 0||strlen( pJobinfo->m_szFileBarcode)==0)
									{
										goto Exit;
									}
									else
									{
										GenLog(DEBUG_INFO, "%s[%d].PrintOneDoc失败k = %d \n", __FILE__, __LINE__,k);
										goto SENDTO_PRINTER_ERROR;
									}
								}
								else
								{
									//一份文件发送到打印机完成
									//新的方法只将任务发送到打印机20140504
									PrinttingJob* pPrinttingJob = new PrinttingJob();
									strcpy(pPrinttingJob->szPrintJobID, pJobinfo->m_PrintJobInfo.szEventCode);
									strcpy(pPrinttingJob->szBarcode, pJobinfo->m_szFileBarcode);
									// 最后一份时提示服务器查询回执单 [6/30/2015 chenhong]
									if (CHDDataCenter::Instance()->GetConsoleData()->m_nAutoRecepit == 1)
									{
										if (k == (pJobinfo->m_PrintJobInfo.nPrintCount-1))
										{
											pPrinttingJob->nReceipt = pJobinfo->m_PrintJobInfo.nReceipt;
										}
									}

									pHDPrinter->m_csSendedLock.Lock();
									pHDPrinter->m_listSendedInfo.InsertAt(pHDPrinter->m_listSendedInfo.GetCount(), pPrinttingJob);
									pHDPrinter->m_csSendedLock.Unlock();


									// 预台账处理 [6/17/2015 chenhong]
									PrintResult printResult;
									strcpy(printResult.m_szFileBarcode, pPrinttingJob->szBarcode);
									strcpy(printResult.m_szFileNo, pPrinttingJob->szPrintJobID);

									JOB_INFO_2  currentJob = {0x00};
									//pHDPrinter->GetPrintResult(pJobinfo,currentJob, 4, &printResult);
									//CResultThread::Instance()->InsertPredictResultList(&printResult);

									memset(pJobinfo->m_szFileBarcode, 0x00, c_nChar64*sizeof(char));
									if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
									{
										if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
										{
											// 【刷卡器显示】 [9/6/2015 haojia]
											TCHAR cShowMsg[MAX_PATH] = {0x00};
											int nLen = 0;
											sprintf(cShowMsg,_T("第%d份已发送至打印机..."),k+1);			
											nLen = strlen(cShowMsg);
											CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
										}
									}
								}
							}
							else
							{
								k--;//防止份数不对
							}

							Sleep(HDAppConfig::Instance()->m_AppConfig.m_nPrintDelayTime*1000);
						}

					}

					//BOOL bResult = pHDPrinter->KillAndRestartInj();
					ShowCursor (FALSE) ;
					SetCursor (LoadCursor (NULL, IDC_ARROW)) ;
					GenLog(DEBUG_INFO, "%s[%d].正常离开打印过程，文件编号：%s\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szEventCode);


					/*TCHAR szFullPath[MAX_PATH] = {0x00};
					TCHAR szDir[_MAX_DIR] = {0x00};
					TCHAR szDrive[_MAX_DRIVE] = {0x00};
					_tsplitpath(pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename, szDrive, szDir, NULL, NULL);
					sprintf(szFullPath,"%s%s",szDrive,szDir);
					GenLog(DEBUG_INFO, "%s[%d].szFullPath:[%s]！\n", __FILE__, __LINE__,szFullPath);
					if(PathIsDirectory(szFullPath))
					{
						DeleteDirectorFile(szFullPath,szFullPath);
					}*/

					/*	char unzippath[MAX_PATH] = {0x00};
					char sidname[260] = {0x00};
					if (IsWin7())
					{

					::GetSID(sidname);
					if(PathIsDirectory("C:\\Recycled"))
					{
					GenLog(DEBUG_INFO, "%s[%d].recycled文件夹存在！\n", __FILE__, __LINE__);
					sprintf(unzippath, "%s%s", "C:\\Recycled\\%s", sidname);
					}
					else
					{
					GenLog(DEBUG_INFO, "%s[%d].recycled文件夹不存在！\n", __FILE__, __LINE__);
					sprintf(unzippath,  "C:\\$Recycle.Bin\\%s", sidname);			
					}
					} 
					pJobinfo->m_JobStatusInfo.m_uPageList.PageList.tqh_first->filename;
					DeleteDirectorFile(unzippath,unzippath);
					GenLog(DEBUG_INFO, "%s[%d].szUnzippedPath:[%s]！\n", __FILE__, __LINE__,unzippath);*/
					goto Exit;
				}
				else
				{
					GenLog(ERROR_INFO, "%s[%d].CheckPrinterStretchDibSupport验证失败！\n",__FILE__,__LINE__);
					goto SENDTO_PRINTER_ERROR;
				}
SENDTO_PRINTER_ERROR:
				//没有发送到打印机，此任务可以重新打印
				bFailedFlag = FALSE;
				CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_JOB_NO_SENDTO_PRINTER, NULL, (LPARAM)pJobinfo->m_PrintJobInfo.szEventCode);
				GenLog(DEBUG_INFO, "%s[%d].文件没有发送到打印机：%s\n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.szEventCode);
					PrintResult printResult;
				strcpy(printResult.m_szFileBarcode, pJobinfo->m_szFileBarcode);
				strcpy(printResult.m_szFileNo, pJobinfo->m_PrintJobInfo.szEventCode);
				sprintf(printResult.m_szPrintPages,"%d",page);
				JOB_INFO_2  currentJob = {0x00};
				pHDPrinter->GetPrintResult(pJobinfo,currentJob, 5, &printResult);
				CResultThread::Instance()->InsertResultList(&printResult,pJobinfo->m_PrintJobInfo.nReceipt);
				memset(pJobinfo->m_szFileBarcode, 0x00, c_nChar64*sizeof(char));

Exit:
				
				if (bFailedFlag)
				{

				}
#ifdef PRINT_RECEIPT_OPEN
				if (pJobinfo->m_PrintJobInfo.nReceipt)
				{
					//CHDDataCenter::Instance()->m_nReceipt = 1;
					// 网络模式 [10/14/2014 chenhong]
					//if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
					if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK||SILENCE_SCREEN == HDAppConfig::Instance()->m_AppConfig.m_nDisplayMode)
					{
						//::SendMessage(AfxGetMainWnd()->GetSafeHwnd(), WM_MESSAGE_CLEAR_JOB_LIST, NULL, NULL);
					}
					else
					{
						// 补打取消提示 [5/5/2015 chenhong]
						if (!pJobinfo->m_bIsAdd)
						{
							if (CHDDataCenter::Instance()->GetConsoleData()->m_nAutoRecepit == 1)
							{
								ShowTipMsg(_T("将自动输出交接单文件，请确认交接单文件！"), c_btnDelayTime);
							}
							else
							{
								ShowTipMsg(_T("请到交接单中打印外发交接单！"), c_btnDelayTime);
							}

						}
					}

				}
#endif//PRINT_ASSESS_OPEN
				pHDPrinter->SetUnSendJob();
				//delete pJobinfo;
				//pJobinfo = NULL;
				// 网络模式 [2/26/2016 chenhong]
				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
				{
					GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
					CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
				}

				if(hdcPrint)
				{
					DeleteDC (hdcPrint);
					hdcPrint = NULL;
				}

				if(ppi2)
				{
					GlobalFree((HGLOBAL)ppi2);
					ppi2 = NULL;
				}

				if(pHDPrinter->m_pEngine)
				{
					delete pHDPrinter->m_pEngine;
					pHDPrinter->m_pEngine = NULL;
				}

			}
			else
			{
				//GenLog(DEBUG_INFO, "%s[%d].m_listJob.GetCount()=[%d],pHDPrinter->IsSendNext()=[%d]\n",__FILE__,__LINE__,pHDPrinter->m_listJob.GetCount(), pHDPrinter->IsSendNext());
			}

		}		
	}

	return 0;
}

/*****************
函数名称: monitor_printer_ex
描述：打印机监控线程,根据打印机名称对打印机进行监控，当打印机上有打印任务时，获取打印任务的打印任务、打印总页数等信息
参数：
lpParameter 打印机id，通过遍历打印机获取
***************/
DWORD WINAPI CHDPrinter::MonitorPrinterThread(PVOID szID)
{
	//打印机名字转id
	char szPath[c_nChar64] = {0x00};
	HANDLE printerHandle = NULL; //打印机名称
	HANDLE chgObject;     //改变状态通知
	DWORD nByteNeeded;
	DWORD nReturned;
	DWORD nByteUsed;
	BOOL fcnreturn;
	DWORD dwChange;
	int iJobNum;  //作业数
	char cPrinterCode[64];   //打印机ID
	int  iPrinterTotalPages; //打印总页数
	int  iPrinterTaskNum;    //打印任务数
	int i;
	int iSubscript; //此打印机的下角标数据

	char lpPrinterId[256] = {0x00};
	char* p;
	p=(char *)szID;
	sprintf(lpPrinterId ,"%s" , p);

	PRINTER_DEFAULTS pDefault;
	ZeroMemory(&pDefault, sizeof(PRINTER_DEFAULTS));
	pDefault.DesiredAccess = PRINTER_ALL_ACCESS;

	PrinterTaskInfo* pPrinter = new PrinterTaskInfo;
	memcpy(pPrinter->cPrinterCode , lpPrinterId , strlen(lpPrinterId));
	CHDDataCenter::Instance()->m_PrinterTaskInfo.InsertAt(CHDDataCenter::Instance()->m_PrinterTaskInfo.GetCount(), pPrinter);


	if (!CDistributeThread::Instance()->GetPrinterIDByPath(lpPrinterId, szPath))
	{
		GenLog(ERROR_INFO,"%s[%d].高级打印中获取打印机%s的路径失败\n", __FILE__, __LINE__, lpPrinterId);
		return FALSE;
	}

	//打开打印机
	int iRet = OpenPrinter(szPath,&printerHandle,NULL);
	if(!iRet)
	{
		GenLog(ERROR_INFO,"%s[%d].打开打印机失败\n", __FILE__, __LINE__, szPath);
		return FALSE;
	}

	//查找打印机数据角标
	for( i = 0 ; i < CHDDataCenter::Instance()->m_PrinterTaskInfo.GetCount() ; i++ )
	{
		if(strcmp(CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(i)->cPrinterCode,lpPrinterId)==0)
		{
			GenLog(ERROR_INFO,"%s[%d].找到对应数据，打印机角标为[%d]\n", __FILE__, __LINE__, i);
			iSubscript = i;
			break;
		}
	}

	chgObject = FindFirstPrinterChangeNotification(printerHandle, PRINTER_CHANGE_JOB,0,NULL);
	while(1)
	{
		WaitForSingleObject(chgObject,INFINITE);
		fcnreturn = FindNextPrinterChangeNotification(chgObject,&dwChange,NULL,NULL);
		if(fcnreturn)
		{
			GenLog(DEBUG_INFO,"%s[%d]document：[%d]\n",__FILE__,__LINE__,dwChange);
			if(dwChange != PRINTER_CHANGE_WRITE_JOB)
			{
				iPrinterTotalPages = 0;
				//通过调用GetPrinter()函数得到作业数量
				PRINTER_INFO_2 *pPrinterInfo = NULL;
				GetPrinter(printerHandle,2,NULL,0,&nByteNeeded);
				pPrinterInfo = (PRINTER_INFO_2 *)malloc(nByteNeeded);
				GetPrinter(printerHandle,2,(LPBYTE)pPrinterInfo,nByteNeeded,&nByteUsed);

				// 通过调用EnumJobs()函数枚举任务
				JOB_INFO_2 *pJobInfo = NULL;
				EnumJobs(printerHandle,0,pPrinterInfo->cJobs,2,NULL,0,(LPDWORD)&nByteNeeded,(LPDWORD)&nReturned);
				pJobInfo = (JOB_INFO_2 *)malloc(nByteNeeded);
				ZeroMemory(pJobInfo,nByteNeeded);
				EnumJobs(printerHandle,0,pPrinterInfo->cJobs,2,(LPBYTE)pJobInfo,nByteNeeded,(LPDWORD)&nByteUsed,(LPDWORD)&nReturned);

				//判断是否有打印任务
				if(pPrinterInfo->cJobs == 0)
				{
					//向数据类中保存数据 add tianlj
					CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(iSubscript)->iPrinterTaskNum = pPrinterInfo->cJobs;
					CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(iSubscript)->iPrinterTotalPages = iPrinterTotalPages;

					GenLog(DEBUG_INFO,"%s[%d]document：[%d]\n",__FILE__,__LINE__,iPrinterTotalPages);
					free(pPrinterInfo);
					free(pJobInfo);
					continue;
				}
				iJobNum = pPrinterInfo->cJobs - 1;
				for( i = 0 ; i <= iJobNum ; i++ )
				{
					//文档名称
					GenLog(DEBUG_INFO,"%s[%d]document：[%s]\n",__FILE__,__LINE__,pJobInfo[i].pDocument);
					GenLog(DEBUG_INFO,"%s[%d]JobId:[%d]\n",__FILE__,__LINE__,pJobInfo[i].JobId);
					GenLog(DEBUG_INFO,"%s[%d]PagesPrinted[%d]\n",__FILE__,__LINE__,pJobInfo[i].PagesPrinted);
					GenLog(DEBUG_INFO,"%s[%d]TotalPages[%d]\n",__FILE__,__LINE__,pJobInfo[i].TotalPages);
					iPrinterTotalPages += (pJobInfo[i].TotalPages - pJobInfo[i].PagesPrinted);
				}
				//向数据类中保存数据 add tianlj
				CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(iSubscript)->iPrinterTaskNum = pPrinterInfo->cJobs;
				CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(iSubscript)->iPrinterTotalPages = iPrinterTotalPages;

				GenLog(DEBUG_INFO,"%s[%d]document：[%d]\n",__FILE__,__LINE__,iPrinterTotalPages);

				free(pPrinterInfo);
				free(pJobInfo);

			}//if(dwChange == PRINTER_CHANGE_ADD_JOB)
		}//if(fcnreturn) end
	}//while(1) end
	ClosePrinter(printerHandle);
	return TRUE;
}

BOOL  CHDPrinter::StartSetPrinter()
{
		//如果structSize过大 则重启监控模块
		Sleep(1*1000);
		//重启进程
		//获取监控模块路径
		TCHAR szFullPath[MAX_PATH] = {0x00}; //执行文件全路径

		GetModuleFileName(NULL , szFullPath , MAX_PATH );
		(_tcsrchr(szFullPath,_T('\\')))[1] = 0;
		//
		CString strPath = szFullPath;
		CString strFullPath = strPath + "SetPrinter.exe";
		if(!FindProcess(strFullPath.GetBuffer(0)))
		{
		//创建进程
		BOOL bRet = TRUE;
		DWORD dwExitCode;
		STARTUPINFO si;
		PROCESS_INFORMATION pi;
		ZeroMemory( &si, sizeof(si) );
		si.cb = sizeof(si);
		ZeroMemory( &pi, sizeof(pi) );
		bRet = CreateProcess(NULL, strFullPath.GetBuffer(0), NULL,           // Process handle not inheritable
			NULL,           // Thread handle not inheritable
			FALSE,          // Set handle inheritance to FALSE
			0,              // No creation flags
			NULL,           // Use parent's environment block
			NULL, 
			&si,
			&pi );
		if( bRet ) //做什么用?
		{													// 关闭子进程的主线程句柄		
			//WaitForSingleObject(pi.hProcess, INFINITE);		// 等待子进程的退出							
			GetExitCodeProcess(&pi.hProcess, &dwExitCode);	// 获取子进程的退出码

			GenLog(DEBUG_INFO,"%s[%d].SetPrinter.exe重启成功！\n",__FILE__,__LINE__);
		}
		else
		{
			GenLog(ERROR_INFO,"%s[%d].SetPrinter.exe重启失败！\n", __FILE__, __LINE__);
		} 
		CloseHandle(pi.hThread);		
		CloseHandle(pi.hProcess); 
		}
		return 0;
}

BOOL  CHDPrinter::Startsyssrv()
{
	//如果structSize过大 则重启监控模块
	WinExec(_T("taskkill -f -im HDinjdlls.exe -im HDinjdlls32.exe -im injdlls.exe -im syssrv.exe"),SW_HIDE);
	Sleep(1*1000);

	CString strFullPath = "C:\\Windows\\syssrv.exe /start";
	//创建进程
	BOOL bRet = TRUE;
	DWORD dwExitCode;
	STARTUPINFO si;
	PROCESS_INFORMATION pi;
	ZeroMemory( &si, sizeof(si) );
	si.cb = sizeof(si);
	ZeroMemory( &pi, sizeof(pi) );
	bRet = CreateProcess(NULL, strFullPath.GetBuffer(0), NULL,           // Process handle not inheritable
		NULL,           // Thread handle not inheritable
		FALSE,          // Set handle inheritance to FALSE
		0,              // No creation flags
		NULL,           // Use parent's environment block
		NULL, 
		&si,
		&pi );
	if( bRet ) //做什么用?
	{													// 关闭子进程的主线程句柄		
		//WaitForSingleObject(pi.hProcess, INFINITE);		// 等待子进程的退出							
		GetExitCodeProcess(&pi.hProcess, &dwExitCode);	// 获取子进程的退出码

		GenLog(DEBUG_INFO,"%s[%d].syssrv.exe重启成功！\n",__FILE__,__LINE__);
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].syssrv.exe重启失败！\n", __FILE__, __LINE__);
	} 
	CloseHandle(pi.hThread);		
	CloseHandle(pi.hProcess); 
	return 0;
}

DWORD WINAPI CHDPrinter::WatchThread(LPVOID lpParam)
{
	CHDPrinter* pHDPrinter = (CHDPrinter*)lpParam;
	DWORD dwTimeInterval = ::GetTickCount();

	DWORD dwClearInterval = ::GetTickCount();
	while (TRUE)
	{
		//关闭线程部分
		DWORD dwRet = WaitForSingleObject(pHDPrinter->m_hWatchStopEvent, 200);
		switch(dwRet)
		{
		case WAIT_OBJECT_0:
			pHDPrinter->ClearSendedJob();
			pHDPrinter->ClearUnSendedJob();
			CDistributeThread::Instance()->ClearUnSendedJob();

			ResetEvent(pHDPrinter->m_hWatchStopEvent);
			SetEvent(pHDPrinter->m_hWatchStoppedEvent);
			return 0;
			break;

		default:
			{
				// 新方法20140504。getcount() == 0 时，不进入循环
				for (int j = 0; j < pHDPrinter->m_listSendedInfo.GetCount(); j++)
				{

					GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
					//被通知有任务发送到打印机了，开始检测这个任务
					//有结果了则置事件给打印线程
					PrinttingJob* pPrintInfo = pHDPrinter->m_listSendedInfo.GetAt(j);
					PrintJob* pJobinfo = pHDPrinter->GetSendedJob(pPrintInfo->szPrintJobID);

					if (pJobinfo == NULL)
					{
						//出错了怎么处理
						GenLog(DEBUG_INFO,"%s[%d].未找到文件条码：%s对应的文件%s\n",__FILE__,__LINE__, pPrintInfo->szBarcode, pPrintInfo->szPrintJobID);
						delete pHDPrinter->m_listSendedInfo.GetAt(j);
						pHDPrinter->m_listSendedInfo.RemoveAt(j);
						j--;//因为清除了一个，所以减一
						continue;
					}


					GenLog(DEBUG_INFO,"%s[%d].开始检测文档打印情况，文件条码：%s\n",__FILE__,__LINE__, pPrintInfo->szBarcode);

					HANDLE hPrinter;
					DEVMODE DevMode = {0}; 
					PRINTER_DEFAULTS PrnDef = { 0, &DevMode, PRINTER_ACCESS_USE };

					int iTimeout = 0;

					JOB_INFO_2  *pJobs=NULL;
					JOB_INFO_2  currentJob = {0x00};
					int         cJobs;
					DWORD       dwPrinterStatus;


					GenLog(DEBUG_INFO,"%s[%d].打开打印机 \n",__FILE__,__LINE__);
					if (!OpenPrinter((LPSTR)pHDPrinter->m_strPrinterPath.GetBuffer(0), &hPrinter, &PrnDef))
					{
						GenLog(ERROR_INFO,"%s[%d].打开打印机：%s失败，文件条码：%s;;;%d;;\n",__FILE__,__LINE__, pHDPrinter->m_strPrinterPath.GetBuffer(0), pPrintInfo->szBarcode,GetLastError());
						// 网络模式 [10/14/2014 chenhong]
						if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
						{
							GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
							CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
						}
						return FALSE;
					}
					if(NULL == hPrinter)
					{
						GenLog(ERROR_INFO,"%s[%d].打开打印机：%s不存在，文件编号：%s\n",__FILE__,__LINE__, pHDPrinter->m_strPrinterPath.GetBuffer(0), pPrintInfo->szBarcode);
						// 网络模式 [10/14/2014 chenhong]
						if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
						{
							GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
							CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
						}
						return FALSE;
					}

					int isFindJob = 0;

					GenLog(DEBUG_INFO,"%s[%d].打印结果 \n",__FILE__,__LINE__);
					//打印结果
					PrintResult printResult;
					strcpy(printResult.m_szFileBarcode, pPrintInfo->szBarcode);
					strcpy(printResult.m_szFileNo, pPrintInfo->szPrintJobID);
					GenLog(DEBUG_INFO,"%s[%d].完成拷贝 \n",__FILE__,__LINE__);

					if (!pHDPrinter->GetQueue(hPrinter, &pJobs, &cJobs, &dwPrinterStatus))
					{
						GenLog(ERROR_INFO,"%s[%d].获取打印队列失败，文件条码：%s!\n",__FILE__,__LINE__, pPrintInfo->szBarcode);
						goto RESULT_NEXTLOOP;
					}

					int isFindJob2 = 0;

					GenLog(DEBUG_INFO,"%s[%d].nPrintType is %d \n",__FILE__,__LINE__, pJobinfo->m_PrintJobInfo.nPrintType);
					GenLog(DEBUG_INFO,"%s[%d].cJobs is %d \n",__FILE__,__LINE__,cJobs);
					if (pJobinfo->m_PrintJobInfo.nPrintType == 6)
					{
						isFindJob2 = 0;
					}
					else
					{
						for (int i = 0; i < cJobs; i++)
						{

							GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
							currentJob = pJobs[i];

							GenLog(DEBUG_INFO,"%s[%d].currentJob.pDocument:%s \n",__FILE__,__LINE__,currentJob.pDocument);
							char *pPos_s3 = NULL,*pPos_e3 = NULL;
							char ConsoleID[c_nChar64]={0x00};
							char docid[32]={0x00};
							if(currentJob.pDocument == NULL)
							{
								continue;
							}

							pPos_s3  = strstr (currentJob.pDocument, (const char *)"-");

							if(pPos_s3 == NULL)
							{
								continue;
							}
							pPos_s3 = pPos_s3 + 1;
							pPos_e3  = strstr(pPos_s3,(const char *)"#");
							memcpy(ConsoleID,pPos_s3,pPos_e3-pPos_s3);

							pPos_s3 = pPos_e3 + 1;
							pPos_s3 = strstr(pPos_s3,(const char *)"-");

							pPos_s3 = pPos_s3 + 1;
							pPos_e3 = strstr(pPos_s3,(const char *)"$");
							memcpy(docid,pPos_s3,pPos_e3-pPos_s3);

							GenLog(DEBUG_INFO,"%s[%d].循环多次判断 \n",__FILE__,__LINE__);

							//判断作业是否在打印队列中
							char m_tchPrinterMessage[MAX_PATH] = {0x00};
							if((!strcmp(docid, pPrintInfo->szBarcode)) \
								&& (pHDPrinter->m_HDAppConfig->m_AppConfig.m_strConsoleID.CompareNoCase(ConsoleID) == 0))
							{
								isFindJob2 = 1;

								GenLog(DEBUG_INFO,"%s[%d]判断作业是否在打印队列中 \n",__FILE__,__LINE__);
								//首先判断打印是否出错
								if((currentJob.Status == JOB_STATUS_ERROR) || 
									(currentJob.Status== JOB_STATUS_OFFLINE) ||
									(currentJob.Status == JOB_STATUS_BLOCKED_DEVQ)
									)
								{
									GenLog(DEBUG_INFO, "%s[%d].%s.JobStatus = %08x,打印过程中出错!\n", __FILE__, __LINE__, pPrintInfo->szBarcode, currentJob.Status);
									//pJobinfo->m_JobStatusInfo.m_nStaus = PRINTERROR;//**********这里应该发消息告诉主界面*********************
									// 生成打印结果，保存到打印列表中
									
									pHDPrinter->GetPrintResult(pJobinfo, currentJob, 1, &printResult);
									SetJob(hPrinter,pJobs[i].JobId,2,LPBYTE(&(pJobs[i])),JOB_CONTROL_DELETE);
									if(NprinterNum>0)
										NprinterNum--;
									
								
									WinExec(_T("taskkill -f -im AcroRd32.exe -im acrobat.exe"),SW_HIDE);
										GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprinterNum);
									NprintNum --;
									GenLog(DEBUG_INFO, "%s[%d].一份打印完成待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
									break;
								}
								// 如果打印页已经打印
								else if((currentJob.Status & JOB_STATUS_PRINTED))
								{
									GenLog(DEBUG_INFO, "%s[%d].%s.JobStatus = %08x,打印成功!\n", __FILE__, __LINE__, pPrintInfo->szBarcode, currentJob.Status);
									//pJobinfo->m_JobStatusInfo.m_nStaus = PRINTTED;//**********这里应该发消息告诉主界面*********************
									if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
									{
										if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
										{
											// 【刷卡器显示】 [9/6/2015 haojia]
											TCHAR cShowMsg[MAX_PATH] = {0x00};
											int nLen = 0;
											strcpy(cShowMsg,_T("打印成功..."));			
											nLen = strlen(cShowMsg);
											CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
										}
										if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 2) //十三所定制
										{
											// 【刷卡器显示】 [9/6/2015 haojia]
											TCHAR cShowMsg[MAX_PATH] = {0x00};
											int nLen = 0;
											sprintf(cShowMsg,_T("%s"),_T("打印成功..."));
											nLen = strlen(cShowMsg);
											CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
										}
									}

									if(NprinterNum>0)
										NprinterNum--;
									
									GenLog(DEBUG_INFO, "%s[%d].关闭!\n", __FILE__, __LINE__);
									Sleep(3);
									WinExec(_T("taskkill -f -im AcroRd32.exe -im acrobat.exe"),SW_HIDE);
										GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprinterNum);
									NprintNum --;
									GenLog(DEBUG_INFO, "%s[%d].一份打印完成待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
									//生成打印结果，保存到打印列表中
									pHDPrinter->GetPrintResult(pJobinfo, currentJob, 2, &printResult);
									break;
								}
								// 如果已经删除打印作业
								else if ((currentJob.Status & JOB_STATUS_DELETED))
								{
									GenLog(DEBUG_INFO, "%s[%d].%s.JobStatus = %08x,已取消打印!\n",__FILE__,__LINE__,pPrintInfo->szBarcode, currentJob.Status);
									//pJobinfo->m_JobStatusInfo.m_nStaus = PRINTCANCEL;//**********这里应该发消息告诉主界面*********************

									//生成打印结果，保存到打印列表中
									pHDPrinter->GetPrintResult(pJobinfo, currentJob, 3, &printResult);
									SetJob(hPrinter,pJobs[i].JobId,2,LPBYTE(&(pJobs[i])),JOB_CONTROL_DELETE);
									if(NprinterNum>0)
										NprinterNum--;
					
									GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprinterNum);
									NprintNum --;
									GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
									
									GenLog(DEBUG_INFO, "%s[%d].关闭!\n", __FILE__, __LINE__);
									Sleep(3);
									WinExec(_T("taskkill -f -im AcroRd32.exe -im acrobat.exe"),SW_HIDE);
									break;
								}
								//打印过程中
								else if (currentJob.Status & JOB_STATUS_DELETING)
								{
									GenLog(DEBUG_INFO, "%s[%d].%s.JobStatus = %08x,正在删除打印任务!\n",__FILE__,__LINE__, pPrintInfo->szBarcode, currentJob.Status);
								}
								else if ((currentJob.Status & JOB_STATUS_PRINTING) || 
									(currentJob.Status & JOB_STATUS_PAUSED) || 
									(currentJob.Status & JOB_STATUS_PAPEROUT) ||
									(currentJob.Status & JOB_STATUS_RESTART) ||
									(currentJob.Status & JOB_STATUS_SPOOLING) ||
									(currentJob.Status & JOB_STATUS_USER_INTERVENTION) ||
									(currentJob.Status & JOB_STATUS_COMPLETE)
									)
								{
									if (currentJob.Status & JOB_STATUS_PAPEROUT)
									{
										_tcscpy(m_tchPrinterMessage,_TEXT("打印机缺纸!"));
										//SendMessage(AfxGetMainWnd()->GetSafeHwnd(), WM_MESSAGE_PRINTER_NO_PAPER, NULL, (LPARAM)pJobinfo);
									}
									else if (currentJob.Status & JOB_STATUS_USER_INTERVENTION)
									{
										_tcscpy(m_tchPrinterMessage,_TEXT("打印机出现错误，需要人工干预!"));
										//SendMessage(AfxGetMainWnd()->GetSafeHwnd(), WM_MESSAGE_PRINTEVENT_USER_INTERVENTION, NULL, (LPARAM)pJobinfo);
									}
									else if (currentJob.Status & JOB_STATUS_PAUSED)
									{
										_tcscpy(m_tchPrinterMessage,_TEXT("打印作业暂停..."));
										//SendMessage(AfxGetMainWnd()->GetSafeHwnd(), WM_MESSAGE_PRINTEVENT_PAUSED, NULL, (LPARAM)pJobinfo);
									}
									else if(currentJob.Status & JOB_STATUS_RESTART)
									{
										_tcscpy(m_tchPrinterMessage,_TEXT("打印作业重新启动!"));
										//SendMessage(AfxGetMainWnd()->GetSafeHwnd(), WM_MESSAGE_PRINTEVENT_RESTART, NULL, (LPARAM)pJobinfo);
									}
									else if(currentJob.Status & JOB_STATUS_SPOOLING)
									{
										_tcscpy(m_tchPrinterMessage,_TEXT("正在进行后台打印!"));
									}
									else if (currentJob.Status & JOB_STATUS_PRINTING)
									{
										_tcscpy(m_tchPrinterMessage,_TEXT("正在打印,请稍候..."));
									}
									if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
									{
										if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
										{
											// 【刷卡器显示】 [9/6/2015 haojia]
											TCHAR cShowMsg[MAX_PATH] = {0x00};
											int nLen = 0;
											strcpy(cShowMsg,_T("打印成功..."));			
											nLen = strlen(m_tchPrinterMessage);
											CHDDataCenter::Instance()->SendUdpNews2NHK(m_tchPrinterMessage,nLen);
										}
										else if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 2) //十三所定制
										{
											// 【刷卡器显示】 [9/6/2015 haojia]
											TCHAR cShowMsg[MAX_PATH] = {0x00};
											int nLen = 0;
											sprintf(cShowMsg,_T("%s"),_T("打印成功..."));
											nLen = strlen(cShowMsg);
											CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
										}
									}
									GenLog(DEBUG_INFO, "%s[%d].文件编号%s.JobStatus = %08x,%s\n",__FILE__,__LINE__, pPrintInfo->szBarcode, currentJob.Status, m_tchPrinterMessage);
								}
								else
								{
									GenLog(DEBUG_INFO, "%s[%d].%s.JobStatus = %08x,打印任务状态不明!\n",__FILE__,__LINE__, pPrintInfo->szBarcode, currentJob.Status);
								}

								//该任务还没形成结果，所以不清除此任务，直接进行下一个
								pPrintInfo->bFind = TRUE;
								goto RESULT_NEXTLOOP;
							}
						}
					}

					//一旦在队列中找不到pj_id所指向的作业，认为作业已完成
					if(!isFindJob2)
					{	

						GenLog(DEBUG_INFO, "%s[%d].%s.JobStatus = %08x     [%s][%d][%d] ,未在打印队列中找到打印任务，认为已发送到打印机，打印成功! \n", \
							__FILE__, __LINE__, pPrintInfo->szBarcode, currentJob.Status ,pHDPrinter->GetPrinterID(),pJobinfo->m_PrintJobInfo.nPrintCount ,pJobinfo->m_PrintJobInfo.nPageCount);
						if(NprinterNum>0)
					NprinterNum--;					
					GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprinterNum);
						NprintNum --;

						
									GenLog(DEBUG_INFO, "%s[%d].关闭!\n", __FILE__, __LINE__);
						//			Sleep(3);
						//WinExec(_T("taskkill -f -im AcroRd32.exe -im acrobat.exe"),SW_HIDE);
						GenLog(DEBUG_INFO, "%s[%d].一份打印完成待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
						//查找打印机数据角标
						for( int i = 0 ; i < CHDDataCenter::Instance()->m_PrinterTaskInfo.GetCount() ; i++ )
						{
							if(strcmp(CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(i)->cPrinterCode,pHDPrinter->GetPrinterID())==0)
							{
								GenLog(ERROR_INFO,"%s[%d].找到对应数据，打印机角标为[%d]\n", __FILE__, __LINE__, i);
								CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(i)->iPrinterTaskNum -= 1 ;
								CHDDataCenter::Instance()->m_PrinterTaskInfo.GetAt(i)->iPrinterTotalPages -= pJobinfo->m_PrintJobInfo.nPageCount;

								break;
							}
						}

						//pJobinfo->m_JobStatusInfo.m_nStaus = PRINTTED;//**********这里应该发消息告诉主界面*********************
						if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
						{
							if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 0)
							{
								// 【刷卡器显示】 [9/6/2015 haojia]
								TCHAR cShowMsg[MAX_PATH] = {0x00};
								int nLen = 0;
								strcpy(cShowMsg,_T("打印成功..."));			
								nLen = strlen(cShowMsg);
								CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
							}
							else if(HDAppConfig::Instance()->m_ExConfig.m_nShowinCardReader == 2) //十三所定制
							{
								// 【刷卡器显示】 [9/6/2015 haojia]
								TCHAR cShowMsg[MAX_PATH] = {0x00};
								int nLen = 0;
								sprintf(cShowMsg,_T("%s"),_T("打印成功..."));
								nLen = strlen(cShowMsg);
								CHDDataCenter::Instance()->SendUdpNews2NHK(cShowMsg,nLen);
							}
						}
						//生成打印结果
						pHDPrinter->GetPrintResult(pJobinfo, currentJob, 2, &printResult);
					}

					//将打印结果放入发送队列
					if (pJobinfo->m_bIsReceipt)
					{
						HDIOCP::Instance()->ReceiptJobPrintEnsure(pPrintInfo->szBarcode, &pJobinfo->m_ReceiptJobInfo);
						// 网络模式 [10/14/2014 chenhong]
						if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
						{
							GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
							CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
						}
					}
					else
					{
						CResultThread::Instance()->InsertResultList(&printResult,pPrintInfo->nReceipt);
					}

					//将此清除
					GenLog(DEBUG_INFO,"%s[%d].文件条码：%s对应的文件%s的结果插入传送队列完成！\n",__FILE__,__LINE__, \
						pPrintInfo->szBarcode, pPrintInfo->szPrintJobID);
					delete pHDPrinter->m_listSendedInfo.GetAt(j);
					pHDPrinter->m_listSendedInfo.RemoveAt(j);
					j--;//因为清除了一个，所以减一
					
					GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprinterNum);

					TCHAR szFullPath[MAX_PATH] = {0x00};
					TCHAR szDir[_MAX_DIR] = {0x00};
					TCHAR szDrive[_MAX_DRIVE] = {0x00};
					::GetModuleFileName(NULL, szFullPath, MAX_PATH);
					_tsplitpath(szFullPath, szDrive, szDir, NULL, NULL);
					TCHAR szSrcPath[MAX_PATH] = {0x00};
					sprintf_s(szSrcPath, MAX_PATH, _T("%s%sconfigex.ini"), szDrive, szDir);
					int m_TopTips = 0;
					m_TopTips = GetPrivateProfileInt("CONFIG_EX", "COP_TIPS", 1, szSrcPath);



					GenLog(DEBUG_INFO, "%s[%d].待打印的文件份数为:[%d]！\n", __FILE__, __LINE__,NprintNum);
					if(NprintNum == 0)
					{

						if (1 == m_TopTips)
						{
							//if(Nmimi != 0 || Nneibu != 0 || Nfeimi != 0)
							//{
							CString str;
							str.Format("打印完成\n 本次打印成功制作机密级:%d份、核心商密级:%d份、秘密级:%d份、普通商密级:%d份、内部级:%d份、非密级:%d份",Njimi,NHXshangmi,Nmimi,NPTshangmi,Nneibu,Nfeimi);
							//MessageBox(NULL, str, _T("航盾控制台"), MB_OK);
							ShowMsgBox(str.GetBuffer(0), MB_OK);
							//}
						}
						GenLog(DEBUG_INFO, "%s[%d].打印完成！\n", __FILE__, __LINE__);
						Njimi = NHXshangmi = Nmimi = NPTshangmi = Nneibu = Nfeimi =0;	
						char DstCodePath[MAX_PATH*2]={0x00};
						char DsttmpCodePath[MAX_PATH*2]={0x00};
						char InstallPath[MAX_PATH*2] = {0x00};
						sprintf_s(InstallPath, MAX_PATH*2, "%s", HDAppConfig::Instance()->m_szRegPath);
						sprintf_s(DsttmpCodePath, "%stmp\\", InstallPath);		//生成条码的存储位置
						sprintf_s(DstCodePath, "%s\\printcache\\barcode", InstallPath);		//生成条码的存储位置
						DeleteDirectorFile(DstCodePath,DstCodePath);
						DeleteDirectorFile(DsttmpCodePath,DsttmpCodePath);

						GenLog(DEBUG_INFO, "%s[%d].文件清理完成！\n", __FILE__, __LINE__);

						int nFlags = HDAppConfig::Instance()->m_ExConfig.m_IsLinux;
						GenLog(DEBUG_INFO, "%s[%d].打印的份数为:[%d]nFlags[%d]m_TopTips[%d]！\n", __FILE__, __LINE__,NprintNum,nFlags,m_TopTips);
						if (2 == nFlags)
						{
							pHDPrinter->Startsyssrv();
							GenLog(DEBUG_INFO, "%s[%d].Startsyssrv！\n", __FILE__, __LINE__);
							//WinExec(_T("taskkill -f -im AcroRd32.exe -im acrobat.exe"),SW_HIDE);
							pHDPrinter->StartSetPrinter();
							GenLog(DEBUG_INFO, "%s[%d].StartSetPrinter！\n", __FILE__, __LINE__);
						}


						GenLog(DEBUG_INFO, "%s[%d].StartSetPrinter！\n", __FILE__, __LINE__);
						char szUnzippedPath[MAX_PATH*2] = {0x00};
						sprintf_s(szUnzippedPath, "%s\\printcache\\unzipped", InstallPath);	
						//清除临时文件和临时目录
						DeleteDirectorFile(szUnzippedPath, szUnzippedPath);
						GenLog(DEBUG_INFO, "%s[%d].szUnzippedPath:[%s]！\n", __FILE__, __LINE__,szUnzippedPath);

					}//服务器





					// 网络模式 [10/14/2014 chenhong]
					if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
					{
						GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
						CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
					}
RESULT_NEXTLOOP:
					//释放pJobs
					if(pJobs)
					{
						free( pJobs );
						pJobs = NULL;
					}

					ClosePrinter(hPrinter);
				}

				//GenLog(DEBUG_INFO,"%s[%d].print \n",__FILE__,__LINE__);

				pHDPrinter->m_csSendedLock.Lock();


				if ((pHDPrinter->m_listSendedInfo.GetCount() == 0) && (pHDPrinter->GetUnSendJobCount() == 0) 
					&& (::GetTickCount() - dwClearInterval > 30*1000))
				{
					if (CDistributeThread::Instance()->CheckUnSendedJob() == 0)
					{	
						GenLog(DEBUG_INFO,"%s[%d].priClearUnSendedJobClearUnSendedJobClearUnSendedJobClearUnSendedJobClearUnSendedJobnt \n",__FILE__,__LINE__);

						pHDPrinter->ClearSendedJob();
						pHDPrinter->ClearUnSendedJob();
						CDistributeThread::Instance()->ClearUnSendedJob();
						dwClearInterval = GetTickCount();

						// 网络模式
						if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
						{
							GenLog(DEBUG_INFO, "%s[%d].WM_MESSAGE_NETWORK_CLOSED \n", __FILE__, __LINE__);
							CHDDataCenter::Instance()->PostMainDlgMsg(WM_MESSAGE_NETWORK_CLOSED);
						}
					}
				}


				pHDPrinter->m_csSendedLock.Unlock();

				//监测打印机部分
				if (::GetTickCount() - dwTimeInterval > c_nTimeInterval*1000)//应放到2734
				{
					DWORD dwRet = pHDPrinter->GetPrinterStatus();
					if (dwRet)
					{
						//打印机状态
						BOOL bOutFlag = FALSE;
						if (dwRet & PRINTER_STATUS_OFFLINE)
						{				
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s离线！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_OFFLINE, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PAUSED)
						{							
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s暂停！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_PAUSED, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PENDING_DELETION)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s being deleted！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PAPER_JAM)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s卡纸！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_PAPERJAM, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PAPER_OUT)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s缺纸！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_PAPEROUT, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_MANUAL_FEED)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s需手动送纸！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PAPER_PROBLEM)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s纸张出现问题！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_PAPER_PROBLEM, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_IO_ACTIVE)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s in an active input/output state！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_BUSY)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s忙碌！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PRINTING)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s正在打印！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_OUTPUT_BIN_FULL)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s output bin is full！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_NOT_AVAILABLE)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s状态不可用！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_NOT_AVAILABLE, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_WAITING)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s处于等待状态！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PROCESSING)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s正在处理打印任务！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_INITIALIZING)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s正在初始化！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_WARMING_UP)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s正在热机！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_TONER_LOW)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s墨粉较少！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_NO_TONER)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s已无墨粉！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_NO_TONER, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_PAGE_PUNT)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s不能打印当前页面！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_PAGE_PUNT, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_USER_INTERVENTION)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s遇到错误需要手动处理！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(PRINTER_STATUS_USER_INTERVENTION, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_OUT_OF_MEMORY)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s内存不足！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_OUT_OF_MEMORY, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_DOOR_OPEN)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s门被打开！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_DOOR_OPEN, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_SERVER_UNKNOWN)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s状态不明！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_POWER_SAVE)
						{
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s能源节省状态！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (dwRet & PRINTER_STATUS_ERROR)
						{							
							bOutFlag = TRUE;
							GenLog(DEBUG_INFO, "%s[%d].打印机%s处于错误状态！\n",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
							CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_PRINTER_STATUS_ERROR, NULL, (LPARAM)pHDPrinter->m_strPrinterName.GetBuffer(0));
						}

						if (bOutFlag)
						{
							dwTimeInterval = ::GetTickCount();
						}
					}
					else
					{
						//打印机不可用状态，如何处理
						//GenLog(DEBUG_INFO, "%s[%d].检测打印机状态不正常！",__FILE__, __LINE__, pHDPrinter->m_strPrinterName.GetBuffer(0));
					}
				}
			}
		}	
		}

		return 0;
}
string stringToutf8(const string & str)
{
	int nwlen = ::MultiByteToWideChar(CP_ACP,0,str.c_str(),-1,NULL,0);
	wchar_t * pwbuf=new wchar_t[nwlen+1];
	ZeroMemory(pwbuf,nwlen*2+2);
	::MultiByteToWideChar(CP_ACP,0,str.c_str(),str.length(),pwbuf,nwlen);
	int nlen= ::WideCharToMultiByte(CP_UTF8,0,pwbuf,-1,NULL,NULL,NULL,NULL);
	char * pbuf=new char[nlen+1];
	ZeroMemory(pbuf,nlen+1);
	::WideCharToMultiByte(CP_UTF8,0,pwbuf,nwlen,pbuf,nlen,NULL,NULL);
	string retstr=pbuf;
	delete[]pwbuf;
	delete[]pbuf;
	pwbuf=NULL;
	pbuf=NULL;

	return  retstr;
}
char * utf8toansi1(char * path)
{
	int nwlen = ::MultiByteToWideChar(CP_UTF8,0,path,-1,NULL,0);
	wchar_t * pwbuf=new wchar_t[nwlen+1];
	memset (pwbuf,0,nwlen*2+2);
	::MultiByteToWideChar(CP_UTF8,0,path,strlen(path),pwbuf,nwlen);
	int nlen= ::WideCharToMultiByte(CP_ACP,0,pwbuf,-1,NULL,NULL,NULL,NULL);
	char * pbuf=new char[nlen+1];
	memset(pbuf,0,nlen+1);
	::WideCharToMultiByte(CP_ACP,0,pwbuf,nwlen,pbuf,nlen,NULL,NULL);
	return pbuf;

}
BOOL CHDPrinter::GetPrintResult(PrintJob* pPrintJob, JOB_INFO_2 pJob, int IsFindJob, PrintResult* pResult)
{
	if (pResult == NULL)
	{
		return FALSE;
	}

	BOOL bFlg = FALSE;
	memset(pResult->m_szFileNo, 0x00, sizeof(char)*64);
	//memset(pJobinfo->m_szFileBarcode, 0x00, sizeof(char)*32);
	memset(pResult->m_szDutyUserID, 0x00, sizeof(char)*64);
	memset(pResult->m_szCreateUserID, 0x00, sizeof(char)*64);
	memset(pResult->m_szPrinterCode, 0x00, sizeof(char)*64);
	memset(pResult->m_szPrinterName, 0x00, sizeof(char)*64);
	memset(pResult->m_szPrintResultDetail, 0x00, sizeof(char)*256);
	memset(pResult->m_szPrintPages, 0x00, sizeof(char)*32);
	pResult->m_nIsAddPrint = 0;
	pResult->m_nPrintResult = 0;

	if(pPrintJob->m_bIsAdd)
	{
		pResult->m_nIsAddPrint = 1;
	}
	else
	{
		pResult->m_nIsAddPrint = 0;
	}

	sprintf(pResult->m_szDutyUserID, "%s", pPrintJob->m_PrintJobInfo.szUserID);
	//sprintf(pResult->m_szCreateUserID,"%s", pPrintJob->m_PrintJobInfo.szUserID);
	strcpy(pResult->m_szCreateUserID, (const char*)pPrintJob->m_szPUserID);

	GenLog(DEBUG_INFO, "%s[%d].m_szDutyUserID：%s\n", __FILE__,__LINE__, pResult->m_szDutyUserID);
	GenLog(DEBUG_INFO, "%s[%d].m_szCreateUserID：%s\n", __FILE__,__LINE__, pResult->m_szCreateUserID);
	strcpy(pResult->m_szFileNo, (const char*)pPrintJob->m_PrintJobInfo.szEventCode);

	strcpy(pResult->m_szPrinterCode, this->m_strPrinterID.GetBuffer(0));

	//strcpy(pResult->m_szPrinterName, this->m_strPrinterName.GetBuffer(0));

	char detail[256] = {0x00};
	if(1 == IsFindJob)
	{	
		if(pJob.Status & JOB_STATUS_ERROR)
		{
			sprintf(detail,"打印作业出错,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else if(pJob.Status & JOB_STATUS_OFFLINE)
		{
			sprintf(detail,"打印机脱机状态,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else if(pJob.Status & JOB_STATUS_BLOCKED_DEVQ)
		{
			sprintf(detail,"打印驱动不能打印当前作业,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else
		{
			sprintf(detail,"未知错误,已打印份数：%d 份!",m_nPrintedCounts);
		}
		strcpy(pResult->m_szPrintResultDetail, detail);

		sprintf(pResult->m_szPrintPages, "%d", pJob.PagesPrinted);

		// 哪些是错误的情况？ [1/19/2017 Administrator]
		if((pJob.Status & JOB_STATUS_ERROR)||(pJob.Status & JOB_STATUS_OFFLINE)||(pJob.Status & JOB_STATUS_BLOCKED_DEVQ))
		{
			pResult->m_nPrintResult = 0;		 
		}
		else
		{
			pResult->m_nPrintResult = 1;		
		}
	}
	else if(2 == IsFindJob)
	{
		if(pJob.Status & JOB_STATUS_PRINTED)
		{
			sprintf(detail,"打印成功,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else if(pJob.Status & JOB_STATUS_COMPLETE)
		{
			sprintf(detail,"打印任务已发送到打印机,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else
		{
			sprintf(detail, "未在队列中找到该任务,已打印份数：%d 份!",m_nPrintedCounts);
		}
		strcpy(pResult->m_szPrintResultDetail, detail);

		pResult->m_nPrintResult = 1;
		if (0 != pPrintJob->m_JobStatusInfo.m_nStartPage && 0 != pPrintJob->m_JobStatusInfo.m_nEndPage)
		{
			//sprintf(pResult->m_szPrintPages, "%d-%d", \
			//	pPrintJob->m_JobStatusInfo.m_nStartPage, pPrintJob->m_JobStatusInfo.m_nEndPage);

			sprintf(pResult->m_szPrintPages, "%d", \
				pPrintJob->m_JobStatusInfo.m_nEndPage - pPrintJob->m_JobStatusInfo.m_nStartPage + 1);

			//pJobinfo->m_nPageCount = pPrintJob->m_JobStatusInfo.m_nEndPage - pPrintJob->m_JobStatusInfo.m_nStartPage + 1;
		}
		else
		{
			sprintf(pResult->m_szPrintPages, "%d", pPrintJob->m_PrintJobInfo.nPageCount);
			//pJobinfo->m_nPageCount = pPrintJob->m_PrintJobInfo.nPageCount;
		}
	}
	else if(3 == IsFindJob)
	{
		sprintf(detail,"用户取消打印,已打印份数：%d 份!",m_nPrintedCounts);
		strcpy(pResult->m_szPrintResultDetail, detail);
		pResult->m_nPrintResult = 0;
		sprintf(pResult->m_szPrintPages, "%d", pJob.PagesPrinted);
		//pJobinfo->m_nPageCount = pJob.PagesPrinted;
	}
	else if(4 == IsFindJob)
	{
		sprintf(detail,"已发送打印机,已打印份数：%d 份!",m_nPrintedCounts);
		strcpy(pResult->m_szPrintResultDetail, detail);
		pResult->m_nPrintResult = 2;
		sprintf(pResult->m_szPrintPages, "%d", pPrintJob->m_PrintJobInfo.nPageCount);
		GenLog(DEBUG_INFO, "%s[%d].生成预台账文件\n", __FILE__,__LINE__);
		bFlg = TRUE;
	}
	else if(5 == IsFindJob)//文件未完整发送打印机，记录发送页数
	{
		if(pJob.Status & JOB_STATUS_PRINTED)
		{
			sprintf(detail,"打印成功,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else if(pJob.Status & JOB_STATUS_COMPLETE)
		{
			sprintf(detail,"打印任务已发送到打印机,已打印份数：%d 份!",m_nPrintedCounts);
		}
		else
		{
			sprintf(detail, "未在队列中找到该任务,已打印份数：%d 份!",m_nPrintedCounts);
		}
		strcpy(pResult->m_szPrintResultDetail, detail);

		pResult->m_nPrintResult = 1;
		
		
	}
	GenLog(DEBUG_INFO, "%s[%d].文件编号：%s，页码范围：%s\n", __FILE__,__LINE__, 
		pResult->m_szFileBarcode, pResult->m_szPrintPages);

	//国产化打印机上传日志时打印机转回utf8
	//判断为国产化服务器需要转码
	int nFlags = HDAppConfig::Instance()->m_ExConfig.m_hdprintertype;

	CTime CtrlTime=CTime::GetCurrentTime();
	//
#ifdef PRINTBURN_TIME
	sprintf(pResult->m_szPrintTime,"%04d-%02d-%02d %02d:%02d:%02d",CtrlTime.GetYear(),CtrlTime.GetMonth(),CtrlTime.GetDay(),CtrlTime.GetHour(),CtrlTime.GetMinute(),CtrlTime.GetSecond());

#endif
	sprintf(pResult->m_szPrinterName, "%s", m_strPrinterName.GetBuffer(0));
	GenLog(DEBUG_INFO, "%s[%d].打印机：%s\n", __FILE__,__LINE__,pResult->m_szPrinterName);
	GenLog(DEBUG_INFO, "%s[%d].打印机：%s\n", __FILE__,__LINE__,m_strPrinterName.GetBuffer(0));
	
	GenLog(DEBUG_INFO, "%s[%d].打印机写台账%s\n", __FILE__,__LINE__,pResult->m_szPrinterName);
	//sprintf(pResult->m_szPrinterName, "%s", m_strPrinterName.GetBuffer(0));

	//WriteResultXML(pPrintJob, pResult->m_szFileBarcode, pJob, IsFindJob);

	WriteResultXML(pResult, pPrintJob->m_bIsReceipt,bFlg);
	if(nFlags==1)
	{
		string printname = this->m_strPrinterName.GetBuffer(0);
		string printername =stringToutf8(printname);
		strcpy(pResult->m_szPrinterName,printername.data());
	}
	else
	{
		sprintf(pResult->m_szPrinterName, "%s", m_strPrinterName.GetBuffer(0));

	}
	return TRUE;
}


int CHDPrinter::WriteResultXML(PrintResult *pResult, BOOL bIsReceipt,BOOL bIsPredict)
{
	char strNum[64]={0};
	char strText[256]={0};
	DWORD dwText[128]= {0};
	char strXMLfilename[512] ={0x00};

	if (bIsReceipt)
	{
		strcat(strXMLfilename, CHDDataCenter::Instance()->GetDirectory(4));
		strcat(strXMLfilename, "printReceipt");
	}
	else
	{
		if (bIsPredict)
		{
			sprintf_s(strXMLfilename, 512, _T("%sPredict\\"), CHDDataCenter::Instance()->GetDirectory(0));
			CreateDirectory(strXMLfilename, NULL);
			strcat(strXMLfilename, _T("Predict"));
		}
		else
		{
			// 网络模式 [10/15/2014 chenhong]
			if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK)
			{
				sprintf_s(strXMLfilename, 512, _T("%s%s\\"), CHDDataCenter::Instance()->GetDirectory(3), HDAppConfig::Instance()->m_AppConfig.m_strConsoleID.GetBuffer(0));
				CreateDirectory(strXMLfilename, NULL);
			}
			else
			{
				strcat(strXMLfilename, CHDDataCenter::Instance()->GetDirectory(3));
			}
			strcat(strXMLfilename, _T("printjob"));
		}
	}
	strcat(strXMLfilename, pResult->m_szFileBarcode);
	strcat(strXMLfilename, ".xml");

	TiXmlDocument cConfigDoc;
	TiXmlDeclaration decl("1.0", "UTF-8", "" );
	cConfigDoc.InsertEndChild( decl ); 
	TiXmlComment comment("Hangdun Console Print Result");
	cConfigDoc.InsertEndChild(comment);


	//创建用户信息节点printjob
	TiXmlElement m_printjob("Console");

	//添加printfileno
	TiXmlElement m_printfileno("PrintFileNO");
	TiXmlText m_Text2((const char *)(pResult->m_szFileNo));
	m_printfileno.InsertEndChild(m_Text2);

	TiXmlElement m_filebarcode("filebarcode");
	TiXmlText m_Text3(pResult->m_szFileBarcode);//补上**********************************************************************************
	m_filebarcode.InsertEndChild(m_Text3);

	TiXmlElement m_DutyUserID("DutyUserID");
	TiXmlText m_Text4((const char *)(pResult->m_szDutyUserID));
	m_DutyUserID.InsertEndChild(m_Text4);

	TiXmlElement m_CreateUserID("CreateUserID");
	TiXmlText m_Text5((const char *)(pResult->m_szCreateUserID));
	m_CreateUserID.InsertEndChild(m_Text5);

	TiXmlElement m_PrinterCode("PrinterCode");
	TiXmlText m_Text6((const char *)(pResult->m_szPrinterCode));
	m_PrinterCode.InsertEndChild(m_Text6);

	TiXmlElement m_PrinterName("PrinterName");
	
	//if(0)
	//{
		
	//	char * printname = utf8toansi1(pResult->m_szPrinterName);
	//}
	//else
	//{	
		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pResult->m_szPrinterName, -1, (LPWSTR)dwText, 128);
		WideCharToMultiByte(CP_UTF8, 0, (LPWSTR)dwText, -1, strText, 256, NULL, NULL);

	//}

	TiXmlText m_Text7((const char *)(strText));
	m_PrinterName.InsertEndChild(m_Text7);
	memset(dwText, 0x00, sizeof(dwText));
	memset(strText, 0x00, sizeof(strText));

	TiXmlElement m_PrintResultDetail("PrintResultDetail");
	MultiByteToWideChar(CP_ACP, 0, (LPCSTR)pResult->m_szPrintResultDetail, -1, (LPWSTR)dwText, 128);
	WideCharToMultiByte(CP_UTF8, 0, (LPWSTR)dwText, -1, strText, 256, NULL, NULL);
	TiXmlText m_Text8(strText);
	m_PrintResultDetail.InsertEndChild(m_Text8);
	memset(dwText, 0x00, sizeof(dwText));
	memset(strText, 0x00, sizeof(strText));

	TiXmlElement m_PrintPages("PrintPages");
	TiXmlText m_Text9((const char *)(pResult->m_szPrintPages));
	m_PrintPages.InsertEndChild(m_Text9);

	TiXmlElement m_IsAddPrint("IsAddPrint");
	itoa(pResult->m_nIsAddPrint, strNum, 10);
	TiXmlText m_Text10(strNum);
	m_IsAddPrint.InsertEndChild(m_Text10);

	TiXmlElement m_PrintResult("PrintResult");
	itoa(pResult->m_nPrintResult, strNum, 10);
	TiXmlText m_Text11(strNum);
	m_PrintResult.InsertEndChild(m_Text11);


#ifdef PRINTBURN_TIME
	TiXmlElement m_PrintTime("PrintTime");
	TiXmlText m_Text12((const char *)(pResult->m_szPrintTime));
	m_PrintTime.InsertEndChild(m_Text12);
#endif
	//链接节点到Console
	m_printjob.InsertEndChild(m_printfileno);
	m_printjob.InsertEndChild(m_filebarcode);
	m_printjob.InsertEndChild(m_DutyUserID);
	m_printjob.InsertEndChild(m_CreateUserID);
	m_printjob.InsertEndChild(m_PrinterCode);
	m_printjob.InsertEndChild(m_PrinterName);
	m_printjob.InsertEndChild(m_PrintResultDetail);
	m_printjob.InsertEndChild(m_PrintPages);
	m_printjob.InsertEndChild(m_IsAddPrint);
	m_printjob.InsertEndChild(m_PrintResult);
//	
#ifdef PRINTBURN_TIME
	m_printjob.InsertEndChild(m_PrintTime);
#endif

	//链接节点到cConfigureInfo

	cConfigDoc.InsertEndChild(m_printjob);
	cConfigDoc.SaveFile(strXMLfilename);
	cConfigDoc.Clear();

	return 0;
}


//************************************
// Method:    GetPaperSizebyName
// FullName:  CHDPrinter::GetPaperSizebyName
// Access:    private 
// Returns:   int
// Qualifier:
// Parameter: TAILQ_FileInfo * fileinfo
// Parameter: char * szPaperName
// Parameter: short * orientation
// 描述： 该函数用于通过 传入的 "A3/A4"等 字符串匹配出相应的 数字。
// 时间： 2014-08-16
// 修改人： 石春刚 、党伟
//************************************
int CHDPrinter::GetPaperSizebyName(int m_PrintJobInfo_nPrintType,TAILQ_FileInfo* fileinfo, char* szPaperName, short* orientation)
{
	//GenLog(DEBUG_INFO, "%s[%d].GetPaperNumbyName前\n");
	int nPaperNO = 0;
	if (fileinfo != NULL)
	{
		//为了取得纸张横纵向
		this->GetPaperSize(m_PrintJobInfo_nPrintType,fileinfo, orientation);
	}
	else
	{
		// 注释返回值，否则不传入 fileinfo ，则无法把纸张大小的 字符串 转换为 数字 [8/16/2014 zhangzhenwei]
		//return nPaperNO;
	}
	//GenLog(DEBUG_INFO, "%s[%d].GetPaperNumbyName：GetPaperSize后\n");
	/////////////////////////////////////////////获取纸张名称////////////////////////////////////////////////////////////
	int namesize = sizeof(char)*64;
	char *printerport="";

	int nosNames = DeviceCapabilities(
		m_strPrinterPath,         // printer name
		printerport,           // port name
		DC_PAPERNAMES,       // device capability
		NULL,          // output buffer
		NULL//CONST DEVMODE *pDevMode  // device data buffer
		);

	if (nosNames <= 0)
	{
		//char lstmp[128]="";
		//MessageBox(NULL,"请确认打印机的驱动是否正常。\n", "航盾打印控制台", c_uintBtnStyle);
		ShowTipMsg(_T("请确认打印机的驱动是否正常！"), c_btnDelayTime);
		return 0;
	}

	char* pOutputNames = new char[(nosNames)*namesize];
	memset(pOutputNames,0,(nosNames)*namesize);
	nosNames = DeviceCapabilities(
		m_strPrinterPath,         // printer name
		printerport,
		DC_PAPERNAMES,       // device capability
		pOutputNames,          // output buffer
		NULL//CONST DEVMODE *pDevMode  // device data buffer
		);

	//GenLog(DEBUG_INFO, "%s[%d].GetPaperNumbyName：获取纸张名称后\n");
	/////////////////////////////////////////////////获取纸张大小///////////////////////////////////////////////////////////////
	int papersize=sizeof(DWORD);

	int nosPapers =DeviceCapabilities(
		m_strPrinterPath,         // printer name
		printerport,           // port name
		DC_PAPERS,       // device capability
		NULL,          // output buffer
		NULL//CONST DEVMODE *pDevMode  // device data buffer
		);

	if (nosPapers <= 0)
	{
		//char lstmp[128]="";
		//MessageBox(NULL,"请确认打印机的驱动是否正常。\n", "航盾打印控制台", c_uintBtnStyle);
		ShowTipMsg(_T("请确认打印机的驱动是否正常！"), c_btnDelayTime);
		return 0;
	}

	char* pOutputNum = new char[nosPapers*papersize];

	nosPapers = DeviceCapabilities(
		m_strPrinterPath,         // printer name
		printerport,
		DC_PAPERS,       // device capability
		pOutputNum,          // output buffer
		NULL//CONST DEVMODE *pDevMode  // device data buffer
		);
	//GenLog(DEBUG_INFO, "%s[%d].GetPaperNumbyName：获取纸张号码后\n");
	/////////////////////////////////////////////////通过名字找号///////////////////////////////////////////////////////////////////
	if (nosPapers == nosNames)
	{
		int matchPaperSize = 0;		//匹配纸张的标志  如果纸张匹配，则为1，否则为0
		for (int i = 0; i < nosPapers; i++)
		{
			char szTmp[64] = {0x00};
			memcpy(szTmp, &pOutputNames[i*sizeof(char)*64], sizeof(char)*64);
			GenLog(DEBUG_INFO, "%s[%d].读取到的纸张名称第%d个 %s\n",__FILE__,__LINE__, i+1, szTmp);

			if (strncmp(szTmp, (const char*)szPaperName, strlen((const char*)szPaperName)) == 0)
			{
				if (IsWow64())
				{
					nPaperNO = pOutputNum[i*2]&0xffff;// 警告：只适合用于32位机，如果为64位，应为0xffff；
				}
				else
				{
					// 修改特殊打印机papersize超过两位字节的计算方案 [8/28/2014 chenhong]
					//nPaperNO = pOutputNum[i*2]&0xff;// 警告：只适合用于32位机，如果为64位，应为0xffff；
					int nHighOrder = 0;
					int nLowOrder = 0;
					nHighOrder = pOutputNum[i*2+1]&0xff;
					nHighOrder = nHighOrder * 256;
					nLowOrder = pOutputNum[i*2]&0xff;
					nPaperNO = nHighOrder + nLowOrder;
					// End [8/28/2014 chenhong]
				}

				matchPaperSize = 1;
				GenLog(ERROR_INFO, "%s[%d].打印机纸张%s = 配置纸张%s, 纸张号码%d\n",__FILE__,__LINE__, szTmp, szPaperName, nPaperNO);
				break;
			}
		}

		if (0 == matchPaperSize)
		{
			GenLog(ERROR_INFO, "%s[%d].纸张名称%s, 没有找到纸张号码\n",__FILE__,__LINE__, szPaperName);
		}
	}
	else
	{
		GenLog(ERROR_INFO, "%s[%d].查找纸张名称数量和纸张号码数量不匹配\n",__FILE__,__LINE__);
	}
	delete pOutputNames;
	pOutputNames = NULL;
	delete pOutputNum;
	pOutputNum = NULL;

	//GenLog(DEBUG_INFO, "%s[%d].GetPaperNumbyName后\n");
	return nPaperNO;
}

//生成条码编号,一维码内容
int CHDPrinter::GenerateBarcode(PrintJob* pJob, BOOL bSel)
{
	GenLog(DEBUG_INFO, _T("%s[%d].CASIC条码开始生成！\n"), __FILE__, __LINE__);
	memset(pJob->m_szFileBarcode, 0x00, sizeof(TCHAR)*c_nChar64);

	int nFlowCode = 0;
	nFlowCode = CDistributeThread::Instance()->ApplyFlowCode();
	if (nFlowCode == 0)
	{
		GenLog(ERROR_INFO, "申请大流水号失败，请检查网络连接\n");
		return nFlowCode;
	}

	TCHAR szFlowCode[c_nChar64] = {0x00};
	sprintf(szFlowCode, "%013d", nFlowCode);

	strcat(pJob->m_szFileBarcode, m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0));//集团标识
	if (pJob->m_PrintJobInfo.nPrintType == 1)
	{
		strcat(pJob->m_szFileBarcode, "D");//载体类型
	}
	else if (pJob->m_PrintJobInfo.nPrintType == 2)
	{
		strcat(pJob->m_szFileBarcode, "T");//载体类型
	}

	//年份
	time_t nowtime = time(NULL);
	tm *pNowtime=NULL;
	pNowtime = localtime(&nowtime);
	CString strYear;
	if (bSel)
	{
		strYear.Format(_T("%04d%02d%02d"), pNowtime->tm_year + 1900, pNowtime->tm_mon + 1, pNowtime->tm_mday);
	}
	else
	{
		strYear.Format(_T("%04d"), pNowtime->tm_year + 1900);
	}
	strcat(pJob->m_szFileBarcode, strYear.GetBuffer(0));
	strYear.ReleaseBuffer();

	//流水号
	strcat(pJob->m_szFileBarcode, szFlowCode);
	strcat(pJob->m_szFileBarcode, "00");

	GenLog(DEBUG_INFO, _T("%s[%d].CASIC条码数值生成[%s]ok！\n"), __FILE__, __LINE__,pJob->m_szFileBarcode);
	return nFlowCode;
}

//生成条码编号,一维码内容
int CHDPrinter::GenerateCAEPBarcode(PrintJob* pJob, BOOL bSel)
{
	GenLog(DEBUG_INFO, _T("%s[%d].CAEP条码开始生成！\n"), __FILE__, __LINE__);
	TCHAR szFlowCode[c_nChar64] = {0x00};
	TCHAR szSeclvType[5] = {0x00};
	int nFlowCode = 0;

	memset(pJob->m_szFileBarcode, 0x00, sizeof(TCHAR)*c_nChar64);

	// 中物院标识 [10/15/2014 chenhong]
	strcat_s(pJob->m_szFileBarcode, m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0));
	strcat_s(pJob->m_szFileBarcode, _T("0"));//系统预留位

	// 组织机构中机构编码 [10/15/2014 chenhong]
	TCHAR szTemp[c_nChar32] = {0x00};
	strcpy(szTemp, CHDDataCenter::Instance()->m_CurrentUser.m_szExtCode);
	strupr(szTemp);
	strcat_s(pJob->m_szFileBarcode, szTemp);

	strcat(pJob->m_szFileBarcode, _T("ZZ"));//载体类型

	// 类型产生方式 [10/15/2014 chenhong]
	strcat(pJob->m_szFileBarcode, _T("DY"));

	// 密级类型 [10/15/2014 chenhong]
	CHDDataCenter::Instance()->GetSecleveCodBySecleveType(pJob->m_PrintJobInfo.nSeclvCode, szSeclvType);
	strcat_s(pJob->m_szFileBarcode, szSeclvType);

	//年份
	time_t nowtime = time(NULL);
	tm *pNowtime=NULL;
	pNowtime = localtime(&nowtime);
	CString strYear;
	if (bSel)
	{
		strYear.Format(_T("%04d%02d%02d"), pNowtime->tm_year + 1900, pNowtime->tm_mon + 1, pNowtime->tm_mday);
	}
	else
	{
		strYear.Format(_T("%04d"), pNowtime->tm_year + 1900);
	}
	strcat(pJob->m_szFileBarcode, strYear.GetBuffer(0));
	strYear.ReleaseBuffer();

	GenLog(DEBUG_INFO, _T("%s[%d].CAEP条码[%s]准备申请流水号！\n"), __FILE__, __LINE__,pJob->m_szFileBarcode);

	//nFlowCode = CDistributeThread::Instance()->ApplyCAEPFlowCode(pJob->m_szFileBarcode, strlen(pJob->m_szFileBarcode));
	nFlowCode = CDistributeThread::Instance()->ApplyFlowCode();
	if (nFlowCode == 0)
	{
		GenLog(ERROR_INFO, _T("CAEP申请大流水号失败，请检查网络连接\n"));
		return nFlowCode;
	}

	//流水号
	sprintf_s(szFlowCode, c_nChar64, _T("%09d"), nFlowCode);	
	strcat(pJob->m_szFileBarcode, szFlowCode);
	// 是否过程文件 [10/31/2014 chenhong]
	if (strlen(pJob->m_PrintJobInfo.szProjectCode) == 0 || pJob->m_PrintJobInfo.szProjectCode == NULL)
	{
		strcat(pJob->m_szFileBarcode,"0");
	}
	else
		strcat(pJob->m_szFileBarcode,pJob->m_PrintJobInfo.szProjectCode);

	GenLog(DEBUG_INFO, _T("%s[%d].CAEP条码数值生成[%s]ok！\n"), __FILE__, __LINE__,pJob->m_szFileBarcode);
	return nFlowCode;
}

int CHDPrinter::GenerateCETCBarcode(PrintJob* pJob, BOOL bSel)
{
	int nFlowCode = 0;
	nFlowCode = CDistributeThread::Instance()->ApplyFlowCode();
	if (nFlowCode == 0)
	{
		GenLog(ERROR_INFO, _T("申请大流水号失败，请检查网络连接\n"));
		return nFlowCode;
	}

	TCHAR szFlowCode[c_nChar64] = {0x00};
	sprintf(szFlowCode, _T("%06d"), nFlowCode);

	strcat(pJob->m_szFileBarcode, m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0));//集团标识
	strcat(pJob->m_szFileBarcode, _T("0"));//预留

	// 成员单位 [7/17/2014 chenhong]
	strcat(pJob->m_szFileBarcode, m_HDAppConfig->m_ExConfig.m_strSubCode);

	// 下属单位 [7/17/2014 chenhong]
	char tmpGroupCode[3] = {0x00};
	// modify [4/3/2015 chenhong]
	//int len = strlen(CHDDataCenter::Instance()->m_CurrentUser.m_szGroupCode);
	//memcpy(tmpGroupCode,&CHDDataCenter::Instance()->m_CurrentUser.m_szGroupCode[len - 2], 3);
	memcpy(tmpGroupCode, CHDDataCenter::Instance()->m_CurrentUser.m_szExtCode,2);
	if (NULL == tmpGroupCode || 0 == strlen(tmpGroupCode))
	{
		int len = strlen(CHDDataCenter::Instance()->m_CurrentUser.m_szGroupCode);
		memcpy(tmpGroupCode,&CHDDataCenter::Instance()->m_CurrentUser.m_szGroupCode[len - 2], 3);
	}
	strupr(tmpGroupCode);
	strcat(pJob->m_szFileBarcode, tmpGroupCode);

	//年份
	time_t nowtime = time(NULL);
	tm *pNowtime=NULL;
	pNowtime = localtime(&nowtime);
	CString strYear;
	strYear.Format(_T("%04d"), pNowtime->tm_year + 1900);
	strcat(pJob->m_szFileBarcode, strYear.GetBuffer(0));
	strYear.ReleaseBuffer();

	//流水号
	strcat(pJob->m_szFileBarcode, szFlowCode);

	GenLog(DEBUG_INFO, _T("%s[%d].CETC条码数值生成[%s]ok！\n"), __FILE__, __LINE__,pJob->m_szFileBarcode);
	return nFlowCode;
}

//生成中电二维码
BOOL CHDPrinter::GenerateCETCBarcode2(PrintJob* pJob,char* barcode)
{
	BOOL bFlag = TRUE;

	if ((pJob == NULL) ||
		(barcode == NULL)
		)
	{
		return FALSE;
	}

	//条码版本标识，中电为CETC-SMZT-2013
	strcat(barcode, m_HDAppConfig->m_ExConfig.m_strCodeVersion.GetBuffer(0));
	strcat(barcode, "^");

	//条码编号
	strcat(barcode, pJob->m_szFileBarcode);
	strcat(barcode, "^");

	//单位
	strcat(barcode, m_HDAppConfig->m_ExConfig.m_strGroupName.GetBuffer(0));
	strcat(barcode, "^");

	//部门
	strcat(barcode, pJob->m_PrintJobInfo.szGroupName);
	strcat(barcode, "^");

	//登记人
	strcat(barcode, (const char*)pJob->m_PrintJobInfo.szUserName);
	strcat(barcode, "^");

	//载体类型，纸质文档
	strcat(barcode, "纸介质");
	strcat(barcode, "^");

	//文件名称
	strcat(barcode, (const char*)pJob->m_PrintJobInfo.szFileName);
	strcat(barcode, "^");

	//密级
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJob->m_PrintJobInfo.nSeclvCode, szSecString);
	strcat(barcode, szSecString);
	strcat(barcode, "^");

	//申请时间
	strcat(barcode, pJob->m_PrintJobInfo.szApplyTime);
	strcat(barcode, "^");

	//来源
	strcat(barcode, "打印");
	strcat(barcode, "^");

	//系统保留字段
	//strcat(barcode, "0");
	//strcat(barcode, "^");
	////备注
	//strcat(barcode, "0");

	//结尾
	strcat(barcode, "|");

	return bFlag;
}

//确认打印机和文件密级是否相符，相符返回TRUE，否则FALSE
BOOL CHDPrinter::CheckPrinterSecLevel(int nFileLevel)
{
	BOOL bFlag = TRUE;

	// 判断是否为交接单无密级输出  [7/22/2015 haojia]
	if(1 == CHDDataCenter::Instance()->nReceiptSeclvFlag)
	{
		bFlag = TRUE;
		return bFlag;
	}

	if(nFileLevel < this->m_nPrinterType)
	{
		bFlag = FALSE;
	}

	/*CString strTmp;
	strTmp.Format(_T("%d"), nFileLevel);*/
	//int nIndex = this->m_strSecAllowed.Find(strTmp);
	//if (nIndex == -1)
	//{
	//	bFlag = FALSE;
	//}
	//else if (nIndex == 0 && this->m_strSecAllowed.GetAt(nIndex+1) == ',')//第一个就是
	//{
	//	bFlag = TRUE;
	//}
	//else if ((nIndex > 0) && (m_strSecAllowed.GetAt(nIndex - 1) == ',') && \
	//	(m_strSecAllowed.GetAt(nIndex + strTmp.GetLength()) == ','))//在中间
	//{
	//	bFlag = TRUE;
	//}
	//else if ((nIndex > 0) && (m_strSecAllowed.GetAt(nIndex - 1) == ',') && \
	//	(m_strSecAllowed.GetLength() == nIndex + strTmp.GetLength()))//在结尾
	//{
	//	bFlag = TRUE;
	//}

	return bFlag;
}

BOOL CHDPrinter::PrintReceipt(PrintJob* pJob, LPDEVMODE devMode, HDC *hdcPrint)
{
	struct TAILQ_FileInfo *AdTable;
	AdTable = new TAILQ_FileInfo;
	char szInstallPath[MAX_PATH] = {0x00};
	//GetConsoleRegPath(szInstallPath);
	sprintf_s(szInstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);

	int nType = 0;


	// 打印外带单
	CString csJobType;
	csJobType.Format("%s", pJob->m_ReceiptJobInfo.szJobTypeCode);
	int nIndex = csJobType.Find(_T("CARRYOUT"));
	if (nIndex != -1)
	{
		// 外带
		nType = 6;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	//判断是否为大唐定制交接单模板   [5/29/2015 haojia]
	else if (RECEIPT_13 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag )
	{
		nType = 7;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else if(RECEIPT_DATANG == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 4;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else if(RECEIPT_7SUO == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 5;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else if(RECEIPT_716 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 8;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else if(RECEIPT_CETC7SUO == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 9;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else if(RECEIPT_31 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 10;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else if(RECEIPT_3bu == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 11;
		//sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
		TCHAR szReciptType[32] = {0x00};
		GetReciptTypeByJobCode(pJob->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, 12);
		}
		else
		{
			sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
		}
	}
	else if(RECEIPT_307 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nType = 13;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}
	else
	{
		nType = 1;
		sprintf(AdTable->filename,"%sTemplate\\%08d.emf", szInstallPath, nType);
	}

	AdTable->offset = 1;

	int status = 0;
	int i = 0;
	int iJobid = 0;

	char szDocNameBuf[MAX_PATH*2]={0x00};
	DOCINFO di = { sizeof (DOCINFO), NULL };
	sprintf(szDocNameBuf,"HDPrint:Console-%s#Document-%s$", m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0), pJob->m_ReceiptJobInfo.szJobCode);
	GenLog(DEBUG_INFO, "%s[%d].(PrintReceipt函数里) pJob->m_ReceiptJobInfo.szJobCode=%s！",__FILE__,__LINE__,  pJob->m_ReceiptJobInfo.szJobCode);
	di.lpszDocName = szDocNameBuf;

	struct TAILQ_FileInfo *next;

	iJobid = StartDoc(*hdcPrint, &di);
	if (iJobid <= 0)
	{
		GenLog(ERROR_INFO, "%s[%d].初始化打印文档错误，%s！",__FILE__,__LINE__, GetErrorMessage());

		CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_JOB_NO_SENDTO_PRINTER, NULL, (LPARAM)pJob->m_PrintJobInfo.szEventCode);
		//MessageBox(NULL,"初始化打印文档错误", "航盾打印控制台", c_uintBtnStyle);
		ShowTipMsg(_T("初始化打印文档错误！"), c_btnDelayTime);

		return FALSE;
	}

	pJob->m_JobStatusInfo.m_nStartPage = 1;	//这步主要是用于AttachBarcode函数
	pJob->m_JobStatusInfo.m_nEndPage = pJob->m_PrintJobInfo.nPageCount;		//这步主要是用于AttachBarcode函数

	devMode->dmPaperSize = 9;//默认A4纸
	devMode->dmOrientation = 1;


	if(ResetDC(*hdcPrint,devMode))
	{
		GenLog(DEBUG_INFO, "%s[%d].Reset DC success ,Orientation value =%d , papersize = %d \n",__FILE__,__LINE__,devMode->dmOrientation, devMode->dmPaperSize);
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].Reset DC failed ,Orientation value =%d , papersize = %d \n",__FILE__,__LINE__,devMode->dmOrientation, devMode->dmPaperSize);
	}

	//大唐交接单模板一页可显示7行文件信息 ，默认模板显示5行   
	int nRows = 0;
	if (RECEIPT_13 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nRows = 3;
	}
	else if(RECEIPT_DATANG == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nRows = 7;
	}
	else
	{
		nRows = 5;
	}

	if (nIndex != -1)
	{
		// 外带
		nRows = 5;
	}

	// add [7/7/2014 Administrator]
	for (int i = 0; i*nRows < CHDDataCenter::Instance()->m_MediaList.GetCount(); i++)
	{
		GenLog(DEBUG_INFO, "%s[%d].打印交接单第[%d]页!\n",__FILE__,__LINE__, i + 1);

		if(SendReceiptJobToPrinter(pJob, AdTable, hdcPrint, i*nRows) != 0)
		{
			ShowTipMsg(_T("可能存在网络导致申请交接单失败，请隔几分钟后重新申请打印交接单！"), c_btnDelayTime);
			GenLog(ERROR_INFO, "%s[%d].打印交接单失败%s!\n",__FILE__,__LINE__, pJob->m_ReceiptJobInfo.szJobCode);
			return FALSE;
		}
		else
		{
			////添加条码
		}
	}	

	int endoc = EndDoc (*hdcPrint);
	if (endoc <= 0)
	{
		GenLog(ERROR_INFO,"%s[%d].EndDoc失败！\n",__FILE__,__LINE__);
	}

	//tempiocp->SendPackage(HEAD_CONSOLE_SEND_TASK_ENSURE, pJob->m_ReceiptJobInfo.sztaskid, strlen(pJob->m_ReceiptJobInfo.sztaskid));
	//SendMessage(hwnd_msg, WM_HDPRINT_SUCCESS, NULL, (LPARAM)pJob);
	delete AdTable;
	return TRUE;
}
int CHDPrinter::SendReceiptJobToPrinter(PrintJob *pJobinfo, TAILQ_FileInfo *fileinfo, HDC *hdc, int nNum)
{
	HENHMETAFILE hemf ;
	RECT rect;
	int printerDpi_X = 600;
	int printerDpi_Y = 600;
	int status = 0;

	char  szCopy[256] = {0x00};

	float fnewx = 0.0;
	float fnewy = 0.0;//条码输出的位置，相对于文档的绝对输出范围
	float fdocx = 0.0;
	float fdocy = 0.0;//文档份数的输出位置，相对于文档的绝对输出范围
	float barcodesize_x = 0.0;
	float barcodesize_y = 0.0;

	printerDpi_X=GetDeviceCaps(*hdc, LOGPIXELSX); //获取设备X轴的DPI
	printerDpi_Y=GetDeviceCaps(*hdc, LOGPIXELSY); //获取设备Y轴的DPI

	barcodesize_x = (38 /25.39999918) * printerDpi_X ; //生成的条码图片原始大小256*48 pixels,实际在纸上大小应该是38mm * 7mm
	barcodesize_y = (7/25.39999918) * printerDpi_Y;

	//基于经验值的页面偏移量,以毫米为单位
	int iExpOffset_up = GetDeviceCaps(*hdc, PHYSICALOFFSETY) ;
	int iExpOffset_left = GetDeviceCaps(*hdc, PHYSICALOFFSETX) ;

	//基于经验值的条码偏移量,以毫米为单位
	int iBarOffset_up = GetDeviceCaps(*hdc, PHYSICALOFFSETY);
	int iBarOffset_left = GetDeviceCaps(*hdc, PHYSICALOFFSETX)*2;
	//GenLog(ERROR_INFO,"%s[%d].条码偏移量：iBarOffset_up = %d，iBarOffset_left = %d\n",iBarOffset_up,iBarOffset_left);

	//emf文件打印
	hemf = GetEnhMetaFile (fileinfo->filename);

	if(!hemf)
	{
		CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_JOB_NO_SENDTO_PRINTER, NULL, (LPARAM)pJobinfo);
		DeleteEnhMetaFile (hemf) ;
		hemf = NULL;
		GenLog(ERROR_INFO,"%s[%d].文件[%s]GetEnhMetaFile 错误\n",fileinfo->filename);
		return -1;
	}

	//可能对rect的修改导致页面放大
	rect.top   = 0 - iExpOffset_up; 
	rect.bottom = (297 /25.39999918) * printerDpi_X - iExpOffset_up;
	rect.left   = 0 - iExpOffset_left;
	rect.right = (210 /25.39999918) * printerDpi_Y - iExpOffset_left;

	ENHMETAHEADER Emf_head;
	if(GetEnhMetaFileHeader(hemf,sizeof(Emf_head), (LPENHMETAHEADER)&Emf_head))
	{
		rect.top   = 0 - iExpOffset_up;		//左上角Y轴坐标
		rect.bottom = (Emf_head.szlMillimeters.cy /25.39999918) * printerDpi_X - iExpOffset_up;		//右下角Y轴坐标
		rect.left   = 0 - iExpOffset_left;		//左上角X轴坐标
		rect.right = (Emf_head.szlMillimeters.cx /25.39999918) * printerDpi_Y - iExpOffset_left;		//右下角X轴坐标
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].GetEnhMetaFileHeader()失败\n",__FILE__,__LINE__);
	}

	//PRINT_SETTING* pSetting = HDIOCP::Instance()->GetPrintSetting(pJobinfo->m_PrintJobInfo.m_nFileType); 
	fnewx = (pJobinfo->m_PrintJobInfo.nCordX /25.39999918) * printerDpi_X;//left

	fnewy = (pJobinfo->m_PrintJobInfo.nCordY/25.39999918) * printerDpi_Y;//up
	if ((StartPage (*hdc) > 0))
	{
		if(!PlayEnhMetaFile (*hdc, hemf, &rect))
		{
			GenLog(ERROR_INFO,"%s[%d].play Enhanced MetaFile Failed:%d\n",__FILE__,__LINE__,GetLastError());
		} 

#ifndef NO_BARCODE

		//打印条码
		if(RECEIPT_DATANG == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			PrintTDReceiptBarcode(pJobinfo, pJobinfo->m_PrintJobInfo.nBarcodeType, hdc, &rect, fnewx, fnewy, nNum);
		}
		else if(RECEIPT_CETC7SUO == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			PrintCETC7SUOReceiptBarcode(pJobinfo, pJobinfo->m_PrintJobInfo.nBarcodeType, hdc, &rect, fnewx, fnewy, nNum);
		}
		else if(RECEIPT_31 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print31SUOReceiptBarcode(pJobinfo, pJobinfo->m_PrintJobInfo.nBarcodeType, hdc, &rect, fnewx, fnewy, nNum);
		}
		else
		{
			int nRet = PrintReceiptBarcode(pJobinfo, pJobinfo->m_PrintJobInfo.nBarcodeType, hdc, &rect, fnewx, fnewy, nNum);
			if(-1 == nRet)
			{
				// 申请条码失败
				GenLog(ERROR_INFO, "%s[%d].PrintReceiptBarcode 失败\n", __FILE__, __LINE__);
				// 删除emf文件，释放指针
				DeleteEnhMetaFile (hemf) ;
				hemf = NULL;
				return -1;
			}
		}
#endif
		// 打印外带单
		CString csJobType;
		csJobType.Format("%s", pJobinfo->m_ReceiptJobInfo.szJobTypeCode);
		int nIndex = csJobType.Find(_T("CARRYOUT"));
		if (nIndex != -1)
		{
			// 外带
			PrintReceiptCarryOutText(pJobinfo, hdc, nNum);
		}
		// 打印外发单
		else if (RECEIPT_13 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print13ReceiptText(pJobinfo, hdc, nNum);
		}
		else if(RECEIPT_DATANG == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)   //大唐交接单定制模板   [5/29/2015 haojia]
		{
			PrintTDReceiptText(pJobinfo,hdc,nNum);
		}
		else if(RECEIPT_7SUO == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print7suoReceiptText(pJobinfo, hdc, nNum);
		}
		else if(RECEIPT_716 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print716ReceiptText(pJobinfo, hdc, nNum);
		}
		else if(RECEIPT_CETC7SUO == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			PrintCETC7SUOReceiptText(pJobinfo, hdc, nNum);
		}
		else if(RECEIPT_31 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print31SUOReceiptText(pJobinfo, hdc, nNum);
		}
		else if(RECEIPT_3bu == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print3buReceiptText(pJobinfo, hdc, nNum);
		}
		else if(RECEIPT_307 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
		{
			Print307ReceiptText(pJobinfo, hdc, nNum);
		}
		else
		{
			PrintReceiptText(pJobinfo, hdc, nNum);
		}

		if (EndPage (*hdc)  <= 0)
		{
			AbortDoc(*hdc);
			GenLog(ERROR_INFO,"%s[%d].文件[%s]EndPage 错误\n",fileinfo->filename);
			status = -1;
		}
	}
	else
	{
		status = -1;
		CHDDataCenter::Instance()->SendMainDlgMsg(WM_MESSAGE_JOB_NO_SENDTO_PRINTER, NULL, (LPARAM)pJobinfo);
		GenLog(ERROR_INFO,"%s[%d].文件[%s]StartPage 错误\n",fileinfo->filename);
	}

	//删除emf文件，释放指针
	DeleteEnhMetaFile (hemf) ;
	hemf = NULL;
	return status;
}

// 中电7所交接单条码打印 [9/25/2018 Administrator]
int CHDPrinter::PrintCETC7SUOReceiptBarcode(PrintJob* pJobinfo,int barcodeType,HDC *hdcPrint,RECT* rect, float fnewx, float fnewy, int nFlag)
{
	// 交接单支持二维码 [4/1/2015 chenhong]
	barcodeType = 1;
	barcodeType = pJobinfo->m_ReceiptJobInfo.nBarcodeType;
	//barcodeType = m_nBarcodeType;
	pJobinfo->m_PrintJobInfo.nPosition = 1;

	char barcode[MAX_PATH] = {0x00};
	// 从服务器获取条码值 [1/8/2015 chenhong]
	if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER)
	{
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=pJobinfo->m_ReceiptJobInfo.nBarcodeType;
		pApplyBarcode.nEventType = 7;//打印1 ，刻录2，交接单7
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
		if (nFlag == 0)
		{
			if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
			{
				GenLog(ERROR_INFO, "%s[%d].打印交接单申请条码失败！", __FILE__, __LINE__);
				return -1;
			}
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));	
		}
	}
	else
	{
		if (nFlag == 0)
		{
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				GenerateCETCBarcode(pJobinfo);
			}
			else
			{
				// 网络模式 [10/15/2014 chenhong]
				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK || strcmp(m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0),"CAEP") == 0)
				{
					if (GenerateCAEPBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
				else
				{
					if (GenerateBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
			}
		}		
	}

	int printerDpi_X=GetDeviceCaps(*hdcPrint, LOGPIXELSX); //获取设备X轴的DPI
	int printerDpi_Y=GetDeviceCaps(*hdcPrint, LOGPIXELSY); //获取设备Y轴的DPI

	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile, ".bmp");

	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*sizeof(char));
		memcpy(barcode, pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode));
	}

	if(barcodeType == GEN39_CODE)
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}
	else if(barcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode);
	}
	else if(barcodeType == PDF417_CODE)
	{	
		// 二维码 [4/1/2015 chenhong]
		GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
		CString strBarcode;
		strBarcode.Format(_T("%s"),pJobinfo->m_szFileBarcode);
		//base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
		GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());

		if(COMPANY_716 == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
		{
			PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT, 3, 20, 0);
		}
		else
		{
			PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT);
		}
		strBarcode.ReleaseBuffer();
	}
	else
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}

	if(!FileIsExits((char *)m39bmpFile) && ((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在!\n",__FILE__,__LINE__,m39bmpFile);
		char InstallPath[MAX_PATH] = {0x00};
		sprintf_s(InstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);
		sprintf(m39bmpFile,"%s\\gougebarcode.bar",InstallPath);
	}

	fnewx += (0.7/25.39999918) * printerDpi_X + 100;
	fnewy += 100;      
	float fOffset = fnewy + 1575.0 + 200;
	float fOffset1 = fOffset + 2330;
	float fY = fnewy/2;       
	if((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE))
	{
		if(barcodeType == GEN39_CODE)
		{
			//一维码
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset1, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset1 += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
		else if(barcodeType == QR_CODE)
		{
			//QR 码
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset, QR_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, QR_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset1, QR_CODE, pJobinfo);
			fnewy = fY + (15/25.39999918) * printerDpi_Y;	//QR
			fOffset += (15/25.39999918) * printerDpi_Y;	//QR
			fOffset1 += (15/25.39999918) * printerDpi_Y;	//QR
		}
		else if(barcodeType == PDF417_CODE)
		{
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset, PDF417_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, PDF417_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset1, PDF417_CODE, pJobinfo);
			//fnewy = fY + (7.5/25.39999918) * printerDpi_Y;		//PDF417
			fnewy = fY + (13/25.39999918) * printerDpi_Y;		//PDF417
			//fOffset += (7.5/25.39999918) * printerDpi_Y;	//QR
			fOffset += (13/25.39999918) * printerDpi_Y;	//QR
			fOffset1 += (13/25.39999918) * printerDpi_Y;	//QR
		}
		else //默认一维码
		{
			//一维码
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset1, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset1 += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
	}
	else //默认一维码
	{
		//一维码
		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset1, GEN39_CODE, pJobinfo);
		fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		fOffset1 += (8.0/25.39999918) * printerDpi_Y;		//Gen39
	}

	//out put characters
	char szTxBuf[1024] = {0x00};
	char tempfiletype[12] = {0x00};
	char temp[6] = {0x00};

	sprintf(szTxBuf,"%s",barcode);	//通用版本

	fnewx += (0.7/25.39999918) * printerDpi_X; 
	this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fOffset, 80);
	this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fnewy, 80);
	this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fOffset1, 80);

	return 0;

}

//大唐条码打印    [5/29/2015 haojia]
int CHDPrinter::PrintTDReceiptBarcode(PrintJob* pJobinfo,int barcodeType,HDC *hdcPrint,RECT* rect, float fnewx, float fnewy, int nFlag)
{
	// 交接单支持二维码 [4/1/2015 chenhong]
	barcodeType = 1;
	//barcodeType = pJobinfo->m_ReceiptJobInfo.nBarcodeType;
	//barcodeType = m_nBarcodeType;
	pJobinfo->m_PrintJobInfo.nPosition = 1;

	char barcode[MAX_PATH] = {0x00};
	// 从服务器获取条码值 [1/8/2015 chenhong]
	if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER)
	{
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=/*pJobinfo->m_PrintJobInfo.nBarcodeType*/1;
		pApplyBarcode.nEventType = 7;//打印1 ，刻录2，交接单7
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
		if (nFlag == 0)
		{
			if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
			{
				GenLog(ERROR_INFO, "%s[%d].打印交接单申请条码失败！", __FILE__, __LINE__);
				return -1;
			}
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));	
		}
	}
	else
	{
		if (nFlag == 0)
		{
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				GenerateCETCBarcode(pJobinfo);
			}
			else
			{
				// 网络模式 [10/15/2014 chenhong]
				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK || strcmp(m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0),"CAEP") == 0)
				{
					if (GenerateCAEPBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
				else
				{
					if (GenerateBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
			}
		}		
	}

	int printerDpi_X=GetDeviceCaps(*hdcPrint, LOGPIXELSX); //获取设备X轴的DPI
	int printerDpi_Y=GetDeviceCaps(*hdcPrint, LOGPIXELSY); //获取设备Y轴的DPI

	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile, ".bmp");

	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*sizeof(char));
		memcpy(barcode, pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode));
	}

	if(barcodeType == GEN39_CODE)
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}
	else if(barcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode);
	}
	else if(barcodeType == PDF417_CODE)
	{	
		// 二维码 [4/1/2015 chenhong]
		GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
		CString strBarcode;
		base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
		GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());

		PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT);
		strBarcode.ReleaseBuffer();
	}
	else
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}

	if(!FileIsExits((char *)m39bmpFile) && ((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在!\n",__FILE__,__LINE__,m39bmpFile);
		char InstallPath[MAX_PATH] = {0x00};
		sprintf_s(InstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);
		sprintf(m39bmpFile,"%s\\gougebarcode.bar",InstallPath);
	}

	fnewx += (0.7/25.39999918) * printerDpi_X + 100;
	fnewy += 100;
	float fOffset = fnewy + 3250.0;
	float fY = fnewy/2;
	if((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE))
	{
		if(barcodeType == GEN39_CODE)
		{
			//一维码

			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
		else if(barcodeType == QR_CODE)
		{
			//QR 码

			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, QR_CODE, pJobinfo);
			fnewy = fY + (15/25.39999918) * printerDpi_Y;	//QR
			fOffset += (15/25.39999918) * printerDpi_Y;	//QR
		}
		else if(barcodeType == PDF417_CODE)
		{

			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, PDF417_CODE, pJobinfo);
			fnewy = fY + (7.5/25.39999918) * printerDpi_Y;		//PDF417
			fOffset += (7.5/25.39999918) * printerDpi_Y;	//QR
		}
		else //默认一维码
		{
			//一维码

			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
	}
	else //默认一维码
	{
		//一维码

		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
		fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
	}

	//out put characters
	char szTxBuf[1024] = {0x00};
	char tempfiletype[12] = {0x00};
	char temp[6] = {0x00};

	sprintf(szTxBuf,"%s",barcode);	//通用版本

	fnewx += (0.7/25.39999918) * printerDpi_X; 

	this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fnewy, 80);

	return 0;
}

int CHDPrinter::PrintReceiptBarcode(PrintJob* pJobinfo,int barcodeType,HDC *hdcPrint,RECT* rect, float fnewx, float fnewy, int nFlag)
{
	// 交接单支持二维码 [4/1/2015 chenhong]
	barcodeType = 1;
	barcodeType = pJobinfo->m_ReceiptJobInfo.nBarcodeType;
	//barcodeType = m_nBarcodeType;
	pJobinfo->m_PrintJobInfo.nPosition = 1;

	char barcode[MAX_PATH] = {0x00};
	// 从服务器获取条码值 [1/8/2015 chenhong]
	if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER||m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_Batch)
	{
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=pJobinfo->m_ReceiptJobInfo.nBarcodeType;
		pApplyBarcode.nEventType = 7;//打印1 ，刻录2，交接单7
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
		if (nFlag == 0)
		{
			if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
			{
				GenLog(ERROR_INFO, "%s[%d].打印交接单申请条码失败！", __FILE__, __LINE__);

				return -1;
			}
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));	
		}
	}
	else
	{
		if (nFlag == 0)
		{
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				GenerateCETCBarcode(pJobinfo);
			}
			else
			{
				// 网络模式 [10/15/2014 chenhong]
				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK || strcmp(m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0),"CAEP") == 0)
				{
					if (GenerateCAEPBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
				else
				{
					if (GenerateBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
			}
		}		
	}

	int printerDpi_X=GetDeviceCaps(*hdcPrint, LOGPIXELSX); //获取设备X轴的DPI
	int printerDpi_Y=GetDeviceCaps(*hdcPrint, LOGPIXELSY); //获取设备Y轴的DPI

	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile, ".bmp");

	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*sizeof(char));
		memcpy(barcode, pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode));
	}

	if(barcodeType == GEN39_CODE)
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}
	else if(barcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode);
	}
	else if(barcodeType == PDF417_CODE)
	{	
		// 二维码 [4/1/2015 chenhong]
		GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
		CString strBarcode;
		strBarcode.Format(_T("%s"),pJobinfo->m_szFileBarcode);
		//base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
		GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());

		if(COMPANY_716 == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
		{
			PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT, 3, 20, 0);
		}
		else
		{
			PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT);
		}
		strBarcode.ReleaseBuffer();
	}
	else
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}

	if(!FileIsExits((char *)m39bmpFile) && ((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在!\n",__FILE__,__LINE__,m39bmpFile);
		char InstallPath[MAX_PATH] = {0x00};
		sprintf_s(InstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);
		sprintf(m39bmpFile,"%s\\gougebarcode.bar",InstallPath);
	}

	fnewx += (0.7/25.39999918) * printerDpi_X + 100;
	fnewy += 100;      
	float fOffset = 0;
	if(RECEIPT_13 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		fOffset = fnewy + 3070.0 + 200;
		GenLog(ERROR_INFO,"%s[%d].13所交接单模板位置!\n",__FILE__,__LINE__);
	}
	else
	{
		fOffset = fnewy + 3250.0 + 200;
	}
	if(RECEIPT_3bu == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		fOffset = fnewy + 3250.0;
		GenLog(ERROR_INFO,"%s[%d].3bu交接单模板位置!\n",__FILE__,__LINE__);
	}
	if(RECEIPT_307 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		fOffset = fnewy + 3050.0;
		GenLog(ERROR_INFO,"%s[%d].3bu交接单模板位置!\n",__FILE__,__LINE__);
	}

	float fY = fnewy/2;
	      
	if((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE))
	{
		if(barcodeType == GEN39_CODE)
		{
			//一维码
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
		else if(barcodeType == QR_CODE)
		{
			//QR 码
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset, QR_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, QR_CODE, pJobinfo);
			fnewy = fY + (15/25.39999918) * printerDpi_Y;	//QR
			fOffset += (15/25.39999918) * printerDpi_Y;	//QR
		}
		else if(barcodeType == PDF417_CODE)
		{
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset, PDF417_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, PDF417_CODE, pJobinfo);
			//fnewy = fY + (7.5/25.39999918) * printerDpi_Y;		//PDF417
			fnewy = fY + (13/25.39999918) * printerDpi_Y;		//PDF417
			//fOffset += (7.5/25.39999918) * printerDpi_Y;	//QR
			fOffset += (13/25.39999918) * printerDpi_Y;	//QR
		}
		else //默认一维码
		{
			//一维码
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
	}
	else //默认一维码
	{
		//一维码
		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
		fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
	}

	//out put characters
	char szTxBuf[1024] = {0x00};
	char tempfiletype[12] = {0x00};
	char temp[6] = {0x00};

	sprintf(szTxBuf,"%s",barcode);	//通用版本

	fnewx += (0.7/25.39999918) * printerDpi_X; 
	if(COMPANY_KEGONGJU == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
	{
		this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fOffset+15, 80);
		this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fnewy+15, 80);
		GenLog(ERROR_INFO,"%s[%d].科工局条码规则，打印交接单!\n",__FILE__,__LINE__);
	}
	else
	{
		this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fOffset, 80);
		this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fnewy, 80);
	}


	return 0;
}

int CHDPrinter::Print31SUOReceiptBarcode(PrintJob* pJobinfo,int barcodeType,HDC *hdcPrint,RECT* rect, float fnewx, float fnewy, int nFlag)
{
	// 交接单支持二维码 [4/1/2015 chenhong]
	barcodeType = 1;
	barcodeType = pJobinfo->m_ReceiptJobInfo.nBarcodeType;
	//barcodeType = m_nBarcodeType;
	pJobinfo->m_PrintJobInfo.nPosition = 1;

	char barcode[MAX_PATH] = {0x00};
	// 从服务器获取条码值 [1/8/2015 chenhong]
	if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER)
	{
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=pJobinfo->m_ReceiptJobInfo.nBarcodeType;
		pApplyBarcode.nEventType = 7;//打印1 ，刻录2，交接单7
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
		if (nFlag == 0)
		{
			if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
			{
				GenLog(ERROR_INFO, "%s[%d].打印交接单申请条码失败！", __FILE__, __LINE__);
				return -1;
			}
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));	
		}
	}
	else
	{
		if (nFlag == 0)
		{
			if (COMPANY_CETC == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
			{
				GenerateCETCBarcode(pJobinfo);
			}
			else
			{
				// 网络模式 [10/15/2014 chenhong]
				if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK || strcmp(m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0),"CAEP") == 0)
				{
					if (GenerateCAEPBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
				else
				{
					if (GenerateBarcode(pJobinfo) == 0)
					{
						GenLog(ERROR_INFO, "%s[%d].打印交接单申请大流水号失败！", __FILE__, __LINE__);
						return -1;
					}
				}
			}
		}		
	}

	int printerDpi_X=GetDeviceCaps(*hdcPrint, LOGPIXELSX); //获取设备X轴的DPI
	int printerDpi_Y=GetDeviceCaps(*hdcPrint, LOGPIXELSY); //获取设备Y轴的DPI

	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile, ".bmp");

	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*sizeof(char));
		memcpy(barcode, pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode));
	}

	if(barcodeType == GEN39_CODE)
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}
	else if(barcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode);
	}
	else if(barcodeType == PDF417_CODE)
	{	
		// 二维码 [4/1/2015 chenhong]
		GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
		CString strBarcode;
		strBarcode.Format(_T("%s"),pJobinfo->m_szFileBarcode);
		//base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
		GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());

		if(COMPANY_716 == HDAppConfig::Instance()->m_ExConfig.m_nCompanyType)
		{
			PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT, 3, 20, 0);
		}
		else
		{
			PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT);
		}
		strBarcode.ReleaseBuffer();
	}
	else
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}

	if(!FileIsExits((char *)m39bmpFile) && ((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在!\n",__FILE__,__LINE__,m39bmpFile);
		char InstallPath[MAX_PATH] = {0x00};
		sprintf_s(InstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);
		sprintf(m39bmpFile,"%s\\gougebarcode.bar",InstallPath);
	}

	fnewx += (0.7/25.39999918) * printerDpi_X + 100;
	fnewy += 100;      
	float fOffset = 0;
	if(RECEIPT_13 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		fOffset = fnewy + 3070.0 + 200;
		GenLog(ERROR_INFO,"%s[%d].13所交接单模板位置!\n",__FILE__,__LINE__);
	}
	else
	{
		fOffset = fnewy + 3250.0 + 200;
	}

	float fY = fnewy/2;       
	if((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE))
	{
		if(barcodeType == GEN39_CODE)
		{
			//一维码
			//HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
		else if(barcodeType == QR_CODE)
		{
			//QR 码
			//HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset, QR_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, QR_CODE, pJobinfo);
			fnewy = fY + (15/25.39999918) * printerDpi_Y;	//QR
			fOffset += (15/25.39999918) * printerDpi_Y;	//QR
		}
		else if(barcodeType == PDF417_CODE)
		{
			//HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fOffset, PDF417_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile,*hdcPrint,rect,fnewx, fY, PDF417_CODE, pJobinfo);
			//fnewy = fY + (7.5/25.39999918) * printerDpi_Y;		//PDF417
			fnewy = fY + (13/25.39999918) * printerDpi_Y;		//PDF417
			//fOffset += (7.5/25.39999918) * printerDpi_Y;	//QR
			fOffset += (13/25.39999918) * printerDpi_Y;	//QR
		}
		else //默认一维码
		{
			//一维码
			//HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
			HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
			fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
			fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		}
	}
	else //默认一维码
	{
		//一维码
		//HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fOffset, GEN39_CODE, pJobinfo);
		HDDrawBitmap(m39bmpFile, *hdcPrint, rect, fnewx, fnewy, GEN39_CODE, pJobinfo);
		fnewy += (8.0/25.39999918) * printerDpi_Y;		//Gen39
		fOffset += (8.0/25.39999918) * printerDpi_Y;		//Gen39
	}

	//out put characters
	char szTxBuf[1024] = {0x00};
	char tempfiletype[12] = {0x00};
	char temp[6] = {0x00};

	sprintf(szTxBuf,"%s",barcode);	//通用版本

	fnewx += (0.7/25.39999918) * printerDpi_X; 
	//this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fOffset, 80);
	this->HDDrawText(szTxBuf,*hdcPrint,fnewx, fnewy, 80);

	return 0;
}
//大唐交接单模板设计  [5/29/2015 haojia]
int CHDPrinter::PrintTDReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	//打印交接单系统时间
	CTime ct = CTime::GetCurrentTime();
	CString ctnow = ct.Format("%Y 年 %m 月 %d 日");

	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;

	//申请部门
	float fxSendGroup = fxCode1 + 70;
	float fySendGroup = fyCode1 + 360;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxSendGroup, fySendGroup, 80);

	//接收单位
	float fxReceiveGroup1  = fxSendGroup;// + 250;
	float fyReceiveGroup1  = fySendGroup + 188;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup1, fyReceiveGroup1,80);

	//接收人员信息
	float fxReceiveUser = fxReceiveGroup1;
	float fyReceiveUser = fyReceiveGroup1 + 188;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxReceiveUser, fyReceiveUser,80);

	//申请人 
	CString strSendPeople;
	strSendPeople.Format(_T("%s"), pJobinfo->m_PrintJobInfo.szUserName);

	float fxSendGroup1 = fxSendGroup + 2000;
	float fySendGroup1 = fySendGroup;
	this->HDDrawText(strSendPeople.GetBuffer(0), *hdcPrint, fxSendGroup1, fySendGroup1, 80);
	strSendPeople.ReleaseBuffer();

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
	float fxMediaType1 = fxReceiveGroup1;
	float fyMediaType1 = fySendGroup1 + 132.5;

	//交接单类型
	float fxMediaReciptType1 = fxReceiveGroup1 + 2000.0;
	float fyMediaReciptType1 = fySendGroup1 + 132.5;

	//介质密级
	float fxLevel1 = fxReceiveGroup1 + 3000.0;
	float fyLevel1 = fyMediaType1;

	//显示统计	
	CString strCount;

	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	float fyCount = fyReceiveUser + 330;        //fyReceiveGroup1 + 660;
	float fxCount = fxReceiveGroup1 + 1820;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyCount + 250;                  //fyReceiveGroup1 + 950;
	float fyMediaName = fyCount + 200;              //fyReceiveGroup1 + 900;
	float fyMediaNameEx = fyCount + 200 + 110;      //fyReceiveGroup1 + 970 + 40;

	float fxMediaName = fxCode1 - 270;             //fxReceiveGroup1;         //介质名称
	float fxMediaCode = fxReceiveGroup1 + 2350;	     //介质条码号
	float fxMediaLevel = fxReceiveGroup1 + 1200;     //介质密级
	float fxMediaNum = fxMediaLevel + 620;       //介质数量
	char szFileName[MAX_PATH] = {0x00};
	char szTempFileName[MAX_PATH] = {0x00};

	float fxMediaStyle = fxMediaLevel + 300;    //介质类型
	float fyPrintTime = fyReceiveUser;    //打印交接单时间

	this->HDDrawText(ctnow.GetBuffer(0), *hdcPrint, fxSendGroup1, fyPrintTime,80);
	ctnow.ReleaseBuffer();

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<7); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格 [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);

		

		if (strlen(szTempFileName) > 36)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<36; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 34)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 72)
			{
				strncpy(szExFileName, szTempFileName + nLen, 36);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}
		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		//显示介质类型 
		TCHAR szReciptType[32] = {0x00};
		GetTDReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
		this->HDDrawText(szReciptType, *hdcPrint, fxMediaStyle, fyMedia,80);

		fyMedia += 290;
		fyMediaName += 290;
		fyMediaNameEx += 290;
	}

	return 0;
}

int CHDPrinter::PrintCETC7SUOReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	GenLog(DEBUG_INFO,"%s[%d].（ReceiptTaskInfo结构体）：交接单报送方式:%d \n", __FILE__, __LINE__, pJobinfo->m_ReceiptJobInfo.iSubmitType);

	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 80;

	//报送单位
	float fxSendGroup1  = fxCode1;// + 250;
	float fySendGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(m_HDAppConfig->m_ExConfig.m_strGroupName, *hdcPrint, fxSendGroup1 - 45, fySendGroup1,80);

	//申请人
	float fxSender = fxSendGroup1 + 250;
	float fySender = fySendGroup1 + 1630;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender, fySender, 80);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	// 接收人
	float fxReceiver  = fxSender;
	float fyReceiver  = fySender + 2270;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxReceiver, fyReceiver + 130, 80);


	//申请时间
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(9);
	GenLog(DEBUG_INFO, "%s[%d].交接单作业申请时间为：[%s]\n",__FILE__,__LINE__, strTime.GetBuffer());
	CString strFind=_T("/");
	int y = strTime.Find(strFind);
	CString year = strTime.Left(y);
	if(y!=-1)
	{
		strTime.Delete(0, y+strFind.GetLength());
	}
	int m = strTime.Find(strFind);
	CString month = strTime.Left(m);
	if(m!=-1)
	{
		strTime.Delete(0, m+strFind.GetLength());
	}
	CString days = strTime;

	// 上联申请时间显示 [9/25/2018 Administrator]
	float fxSendYearTime = fxSender + 2550;//1175;
	float fySendYearTime = fySendGroup1;
	this->HDDrawText(year.GetBuffer(0), *hdcPrint, fxSendYearTime, fySendYearTime, 80);
	float fxSendMonthTime = fxSendYearTime + 450;//1175;
	this->HDDrawText(month.GetBuffer(0), *hdcPrint, fxSendMonthTime, fySendYearTime, 80);
	float fxSendDaysTime = fxCode1 - 100;//1175;
	float fySendDaysTime = fySendGroup1 + 200;
	this->HDDrawText(days.GetBuffer(0), *hdcPrint, fxSendDaysTime, fySendDaysTime, 80);

	//// 中间联申请时间显示 [9/25/2018 Administrator]
	float fxSendYearTime1 = fxSender + 1800;//1175;
	float fySendYearTime1 = fySender;
	this->HDDrawText(year.GetBuffer(0), *hdcPrint, fxSendYearTime1, fySendYearTime1, 80);
	float fxSendMonthTime1 = fxSendYearTime1 + 450;//1175;
	this->HDDrawText(month.GetBuffer(0), *hdcPrint, fxSendMonthTime1, fySendYearTime1, 80);
	float fxSendDaysTime1 = fxSendMonthTime1 + 200;//1175;
	this->HDDrawText(days.GetBuffer(0), *hdcPrint, fxSendDaysTime1, fySendYearTime1, 80);

	strTime.ReleaseBuffer();

	int nMimi = 0;
	int nJimi = 0;

	for (int i = 0; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()); i++)
	{

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		if(strcmp(szLevelString, "秘密") == 0)
		{
			nMimi++;
		}
		if(strcmp(szLevelString, "机密") == 0)
		{
			nJimi++;
		}
	}

	int nTotal = nMimi + nJimi;

	// 载体总份数，各个密级份数、份数
	CString csTotalCount;
	csTotalCount.Format("%d", nTotal);
	CString csJimiCount;
	csJimiCount.Format("%d", nJimi);
	CString csMimiCount;
	csMimiCount.Format("%d", nMimi);
	// 上联 [9/25/2018 Administrator]
	float fxTotalCout = fxCode1 + 1500;
	float fyTotalCout = fySendDaysTime;
	this->HDDrawText(csTotalCount.GetBuffer(0), *hdcPrint, fxTotalCout, fyTotalCout, 80);
	float fxnJimiCount = fxSendMonthTime;
	this->HDDrawText(csJimiCount.GetBuffer(0), *hdcPrint, fxnJimiCount, fyTotalCout, 80);
	float fxnMimiCount = fxCode1 + 400;
	float fynMimiCount = fySendDaysTime + 200;
	this->HDDrawText(csMimiCount.GetBuffer(0), *hdcPrint, fxnMimiCount, fynMimiCount, 80);

	// 中间联 [9/25/2018 Administrator]
	float fxTotalCout1 = fxCode1 + 870;
	float fyTotalCout1 = fySender + 200;
	this->HDDrawText(csTotalCount.GetBuffer(0), *hdcPrint, fxTotalCout1, fyTotalCout1, 80);
	float fxnJimiCount1 = fxSendMonthTime1 + 100;
	this->HDDrawText(csJimiCount.GetBuffer(0), *hdcPrint, fxnJimiCount1, fyTotalCout1, 80);
	float fxnMimiCount1 = fxCode1 - 80;
	float fynMimiCount1 = fyTotalCout1 + 200;
	this->HDDrawText(csMimiCount.GetBuffer(0), *hdcPrint, fxnMimiCount1, fynMimiCount1, 80);

	// 下联 [9/25/2018 Administrator]
	float fxTotalCout2 = fxSendMonthTime;
	float fyTotalCout2 = fyReceiver;
	this->HDDrawText(csTotalCount.GetBuffer(0), *hdcPrint, fxTotalCout2, fyTotalCout2 + 130, 80);
	float fxnJimiCount2 = fxCode1 + 1400;
	this->HDDrawText(csJimiCount.GetBuffer(0), *hdcPrint, fxnJimiCount2, fyTotalCout2 + 330, 80);
	float fxnMimiCount2 = fxSender + 1930;
	this->HDDrawText(csMimiCount.GetBuffer(0), *hdcPrint, fxnMimiCount2, fyTotalCout2 + 330, 80);
	return 0;

}

// 通用交接单
int CHDPrinter::PrintReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	GenLog(DEBUG_INFO,"%s[%d].（ReceiptTaskInfo结构体）：交接单报送方式:%d \n", __FILE__, __LINE__, pJobinfo->m_ReceiptJobInfo.iSubmitType);

	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);
	GenLog(DEBUG_INFO, "%s[%d].交接单内容起始坐标fxCode1 = %f，fyCode1 = %f\n",__FILE__,__LINE__, fxCode1, fyCode1);

	//接收单位
	float fxReceiveGroup1  = fxCode1;// + 250;
	float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup1, fyReceiveGroup1,80);

	// 接收人
	float fxReceiverGroup1  = fxCode1 + 2200;// + 250;
	//float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxReceiverGroup1, fyReceiveGroup1, 80);

	//报送单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s %s"), m_HDAppConfig->m_ExConfig.m_strGroupName, pJobinfo->m_ReceiptJobInfo.szUserName);

	float fxSendGroup1 = fxReceiveGroup1;
	float fySendGroup1 = fyReceiveGroup1 + 130;
	this->HDDrawText(strSendGroup.GetBuffer(0), *hdcPrint, fxSendGroup1, fySendGroup1, 80);
	strSendGroup.ReleaseBuffer();


	//报送方式
	float fxSubmit = fxReceiveGroup1 + 3200.0;
	float fySubmit = fySendGroup1;

	GenLog(DEBUG_INFO,"%s[%d].通用交接单介质类型pJobinfo->m_ReceiptJobInfo.iSubmitType为:%d \n", __FILE__, __LINE__, pJobinfo->m_ReceiptJobInfo.iSubmitType);
	if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 1)
	{
		this->HDDrawText("专送", *hdcPrint, fxSubmit, fySubmit,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 0)
	{
		this->HDDrawText("机要", *hdcPrint, fxSubmit, fySubmit,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 2)
	{
		this->HDDrawText("邮政EMS", *hdcPrint, fxSubmit, fySubmit,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 3)
	{
		this->HDDrawText("外单位取走", *hdcPrint, fxSubmit-95, fySubmit,75);	
	}
	else
	{
		this->HDDrawText("无", *hdcPrint, fxSubmit, fySubmit,80);
	}

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GenLog(DEBUG_INFO,"%s[%d].通用交接单介质类型pJobinfo->m_ReceiptJobInfo.szJobTypeCode为:%s \n", __FILE__, __LINE__, pJobinfo->m_ReceiptJobInfo.szJobTypeCode);
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxReceiveGroup1;
	float fyMediaType1 = fySendGroup1 + 132.5;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1,80);

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	float fxMediaReciptType1 = fxReceiveGroup1 + 2000.0;
	float fyMediaReciptType1 = fySendGroup1 + 132.5;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType1, fyMediaReciptType1,80);

	//介质密级   右上角
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_ReceiptJobInfo.nSeclv, szSecString);

	float fxLevel1 = fxReceiveGroup1 + 3200.0;
	float fyLevel1 = fyMediaType1;
	this->HDDrawText(szSecString, *hdcPrint, fxLevel1, fyLevel1,80);

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	float fyCount = fyMediaType1 + 132.5;
	float fxCount = fxReceiveGroup1 + 3050;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaType1 + 310;

	float fxMediaCode1 = fxReceiveGroup1+720;
	float fxMediaCode = fxReceiveGroup1 - 220;
	float fxMediaName = fxMediaCode + 1100;	
	float fxMediaLevel = fxMediaName + 1750;
	float fxMediaNum = fxMediaLevel + 420;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 275;
	float fyMediaNameEx = fyMediaType1 + 375;
	char szTempFileName[MAX_PATH] = {0x00};

	int nFeimi = 0;
	int nNeibu = 0;
	int nPutongshangmi = 0;
	int nMimi = 0;
	int nJimi = 0;
	int nHexinshangmi = 0;
	int nTotalCount = 0;

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		GenLog(DEBUG_INFO, "%s[%d].交接单CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName:%s\n",__FILE__,__LINE__, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		//WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);
		int nFlags = HDAppConfig::Instance()->m_ExConfig.m_hdnservertype;

		GenLog(DEBUG_INFO, "%s[%d].交接单m_hdnservertype:%d\n",__FILE__,__LINE__, nFlags);
		/*if(nFlags==1)
		{
		int nwlen = ::MultiByteToWideChar(CP_UTF8,0,szTempFileName,-1,NULL,0);
		wchar_t * pwbuf=new wchar_t[nwlen+1];
		memset (pwbuf,0,nwlen*2+2);
		::MultiByteToWideChar(CP_UTF8,0,szTempFileName,strlen(szTempFileName),pwbuf,nwlen);
		int nlen= ::WideCharToMultiByte(CP_ACP,0,pwbuf,-1,NULL,NULL,NULL,NULL);
		char * pbuf=new char[nlen+1];
		memset(pbuf,0,nlen+1);
		::WideCharToMultiByte(CP_ACP,0,pwbuf,nwlen,pbuf,nlen,NULL,NULL);
		strcpy(szTempFileName,pbuf);
		GenLog(DEBUG_INFO,"%s[%d].国产化转码[%s]\n", __FILE__, __LINE__,szTempFileName);
		}
		else
		{*/

			MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
			WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);
			strcpy(szTempFileName,temp);
		//}
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		//屏蔽载体名称

		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		if (strlen(szTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
				strncpy(szExFileName, szTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}
		
		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode1, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		/*if(strcmp(szLevelString, "非密") == 0)
		{
		nFeimi++;
		}
		if(strcmp(szLevelString, "内部") == 0)
		{
		nNeibu++;
		}
		if(strcmp(szLevelString, "普通商密") == 0)
		{
		nPutongshangmi++;
		}
		if(strcmp(szLevelString, "秘密") == 0)
		{
		nMimi++;
		}
		if(strcmp(szLevelString, "机密") == 0)
		{
		nJimi++;
		}
		if(strcmp(szLevelString, "核心商密") == 0)
		{
		nHexinshangmi++;
		}*/

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyMediaName += 270;
		fyMediaNameEx += 270;

	}

	// 载体总份数，各个密级份数、份数
	//float fxJobCount1 = fxCode1 + 2000;// + 250;
	//float fyJobCount1 = fyCode1 + 185 - 210 - 132*3;
	//float fyEcohSecTypeOne1  = fyCode1 + 185 - 210 - 132*2;
	//float fyEcohSecTypeTwo1  = fyCode1 + 185 - 210 - 132*1;

	//nTotalCount = nFeimi + nNeibu + nMimi + nJimi + nPutongshangmi + nHexinshangmi;
	//CString csTotalCount;
	//csTotalCount.Format("本次外发共计 %d 份", nTotalCount);
	//CString csEcohSecTypeOne;
	//csEcohSecTypeOne.Format("非密: %d 份;内部: %d 份;普通商密: %d 份", nFeimi, nNeibu, nPutongshangmi);
	//CString csEcohSecTypeTwo;
	//csEcohSecTypeTwo.Format("秘密: %d 份;机密: %d 份;核心商密: %d 份", nMimi, nJimi, nHexinshangmi);

	//this->HDDrawText(csTotalCount.GetBuffer(), *hdcPrint, fxJobCount1, fyJobCount1,60);
	//this->HDDrawText(csEcohSecTypeOne.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeOne1,60);
	//this->HDDrawText(csEcohSecTypeTwo.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeTwo1,60);

	//float fyJobCount2 = fyCode1 + 3300 - 257 + 200 - 132*3;
	//float fyEcohSecTypeOne2  = fyCode1 + 3300 - 257 + 200 - 132*2;
	//float fyEcohSecTypeTwo2  = fyCode1 + 3300 - 257 + 200 - 132*1;
	//this->HDDrawText(csTotalCount.GetBuffer(), *hdcPrint, fxJobCount1, fyJobCount2, 60);
	//this->HDDrawText(csEcohSecTypeOne.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeOne2, 60);
	//this->HDDrawText(csEcohSecTypeTwo.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeTwo2, 60);

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 3300 - 257;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	//接收单位
	float fxReceiveGroup2  = fxCode2;// + 300;
	float fyReceiveGroup2  = fyCode2 + 200;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup2, fyReceiveGroup2,80);


	// 接收人
	float fxReceiverGroup2  = fxCode2 + 2200;// + 250;
	//float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxReceiverGroup2, fyReceiveGroup2, 80);
	//GenLog(DEBUG_INFO, "%s[%d].交接单接收人：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szReceiver);

	//申请人
	float fxSender = fxReceiveGroup2;
	float fySender = fyReceiveGroup2 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender, fySender, 80);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	//申请时间
	float fxSendTime = fxSender + 1100;//1175;
	float fySendTime = fySender;
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(18);
	this->HDDrawText(strTime.GetBuffer(0), *hdcPrint, fxSendTime, fySendTime, 80);
	strTime.ReleaseBuffer();

	//申请部门
	float fxSendGroup = fxSendTime + 1450;
	float fySendGroup = fySender;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxSendGroup, fySendGroup, 80);

	//报送单位
	float fxSendGroup2 = fxReceiveGroup2;
	float fySendGroup2 = fySender + 130;
	this->HDDrawText(m_HDAppConfig->m_ExConfig.m_strGroupName, *hdcPrint, fxSendGroup2, fySendGroup2,80);

	//报送方式
	float fxSubmit2 = fxReceiveGroup2 + 3200.0;
	float fySubmit2 = fySendGroup2;
	if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 1)
	{
		this->HDDrawText("专送", *hdcPrint, fxSubmit2, fySubmit2,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 0)
	{
		this->HDDrawText("机要", *hdcPrint, fxSubmit2, fySubmit2,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 2)
	{
		this->HDDrawText("邮政EMS", *hdcPrint, fxSubmit2, fySubmit2,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 3)
	{
		this->HDDrawText("外单位取走", *hdcPrint, fxSubmit2-95, fySubmit2,75);	
	}
	else
	{
		this->HDDrawText("无", *hdcPrint, fxSubmit2, fySubmit2,80);
	}

	//介质类型
	float fxMediaType2 = fxReceiveGroup2;
	float fyMediaType2 = fySendGroup2 + 130;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2,80);

	//交接单类型
	float fxMediaReciptType2 = fxReceiveGroup2 + 2000.0;
	float fyMediaReciptType2 = fySendGroup2 + 130;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType2, fyMediaReciptType2,80);

	//介质密级   右上角
	float fxLevel2 = fxMediaType2 + 3200.0;
	float fyLevel2 = fyMediaType2;
	this->HDDrawText(szSecString, *hdcPrint, fxLevel2, fyLevel2, 80);

	//显示统计	
	float fyCount2 = fyMediaType2 + 132.5;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	strCount.ReleaseBuffer();
	//介质详细信息
	fyMedia = fyMediaType2 + 310;

	//fxMediaCode1 = fxReceiveGroup2 +720;// - 220;

	fxMediaCode = fxReceiveGroup2  - 220;
	fxMediaName = fxMediaCode + 1100;	
	fxMediaLevel = fxMediaName + 1750;
	fxMediaNum = fxMediaLevel + 420;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 275;
	float fyDownMediaNameEx = fyMediaType2 + 375;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);
		GenLog(DEBUG_INFO, "%s[%d].交接单CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName:%s\n",__FILE__,__LINE__, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szDownTempFileName);
		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szDownTempFileName);
		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		int nFlags = HDAppConfig::Instance()->m_ExConfig.m_hdnservertype;

		/*GenLog(DEBUG_INFO, "%s[%d].交接单m_hdnservertype:%d\n",__FILE__,__LINE__, nFlags);
		if(nFlags==1)
		{
		int nwlen = ::MultiByteToWideChar(CP_UTF8,0,szDownTempFileName,-1,NULL,0);
		wchar_t * pwbuf=new wchar_t[nwlen+1];
		memset (pwbuf,0,nwlen*2+2);
		::MultiByteToWideChar(CP_UTF8,0,szDownTempFileName,strlen(szDownTempFileName),pwbuf,nwlen);
		int nlen= ::WideCharToMultiByte(CP_ACP,0,pwbuf,-1,NULL,NULL,NULL,NULL);
		char * pbuf=new char[nlen+1];
		memset(pbuf,0,nlen+1);
		::WideCharToMultiByte(CP_ACP,0,pwbuf,nwlen,pbuf,nlen,NULL,NULL);
		strcpy(szDownTempFileName,pbuf);
		GenLog(DEBUG_INFO,"%s[%d].国产化转码[%s]\n", __FILE__, __LINE__,szDownTempFileName);
		}
		else
		{*/

				MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
				WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);
				strcpy(szDownTempFileName,temp);
		//}
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szDownTempFileName);
		//屏蔽载体名称
		if (strlen(szDownTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szDownTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szDownTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
			TCHAR szExFileName[60] = {0x00};
			if(strlen(szDownTempFileName) > 80)
			{
				strncpy(szExFileName, szDownTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szDownTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szDownTempFileName);

			GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

		}
		
		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode1, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyDownMediaName += 270;
		fyDownMediaNameEx += 270;
	}

	return 0;
}

int CHDPrinter::Print31SUOReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	GenLog(DEBUG_INFO,"%s[%d].（ReceiptTaskInfo结构体）：交接单报送方式:%d \n", __FILE__, __LINE__, pJobinfo->m_ReceiptJobInfo.iSubmitType);

	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;

	//申请人
	float fxSender = fxCode1;
	float fySender = fxCode1 + 100;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender, fySender, 80);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	//申请时间
	float fxSendTime = fxSender + 1100;//1175;
	float fySendTime = fySender;
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(18);
	this->HDDrawText(strTime.GetBuffer(0), *hdcPrint, fxSendTime, fySendTime, 80);
	strTime.ReleaseBuffer();

	//申请部门
	float fxSendGroup = fxSendTime + 1400;
	float fySendGroup = fySender;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxSendGroup, fySendGroup, 80);


	//报送单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s %s"), m_HDAppConfig->m_ExConfig.m_strGroupName, pJobinfo->m_ReceiptJobInfo.szUserName);

	float fxSendGroup1 = fxSender;
	float fySendGroup1 = fySendGroup + 150;
	this->HDDrawText(strSendGroup.GetBuffer(0), *hdcPrint, fxSendGroup1, fySendGroup1, 80);
	strSendGroup.ReleaseBuffer();


	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxSender;
	float fyMediaType1 = fySendGroup1 + 150;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1,80);


	//接收单位
	float fxReceiveGroup1  = fxSender + 400;// + 250;
	float fyReceiveGroup1  = fyCode1 +2750;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup1, fyReceiveGroup1,80);
	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	float fxCount = fxReceiveGroup1 + 2600;
	float fyCount = fyMediaType1 + 200;

	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaType1 + 415;

	float fxMediaCode = fxReceiveGroup1 - 585;
	float fxMediaName = fxMediaCode + 1050;	
	float fxMediaLevel = fxMediaName + 1735;
	float fxMediaNum = fxMediaLevel + 450;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 350;
	float fyMediaNameEx = fyMediaType1 + 450;
	char szTempFileName[MAX_PATH] = {0x00};

	int nFeimi = 0;
	int nNeibu = 0;
	int nPutongshangmi = 0;
	int nMimi = 0;
	int nJimi = 0;
	int nHexinshangmi = 0;
	int nTotalCount = 0;

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);

		if (strlen(szTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
				strncpy(szExFileName, szTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyMediaName += 270;
		fyMediaNameEx += 270;

	}

	return 0;
}

int CHDPrinter::Print307ReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);

	// 部门/单位
	float fxUserDept1  = fxCode1;// + 250;//1000
	float fyUserDept1  = fyCode1 + 185 - 210;//875
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxUserDept1, fyUserDept1, 80);

	// 接受人
	float fxUser1  = fxCode1 + 2000;// + 250;
	float fyUser1  = fyUserDept1;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxUser1, fyUser1, 80);

	// 接受单位
	float fxSendGroup1 = fxCode1+1400;
	float fySendGroup1 = fyUser1 + 130;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxUserDept1, fySendGroup1, 80);

	// 申请人
	float fxSendUser1 = fxCode1 + 2700; 
	float fySendUser1 = fySendGroup1;
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSendUser1, fySendUser1, 80);


	//报送方式
	float fxSubmitType = fxCode1 +3050; 
	float fySubmitType = fySendGroup1;
	if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 1)
	{
		this->HDDrawText("专送", *hdcPrint,fxSubmitType, fySubmitType,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 0)
	{
		this->HDDrawText("机要", *hdcPrint, fxSubmitType, fySubmitType,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 2)
	{
		this->HDDrawText("邮政EMS", *hdcPrint, fxSubmitType, fySubmitType,80);
	}
	else
	{
		this->HDDrawText("无", *hdcPrint, fxSubmitType, fySubmitType,80);
	}

	// 载体类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxCode1;
	float fyMediaType1 = fySendUser1 + 150;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1, 80);//纸质

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	float fxMediaReciptType1 = fxCode1 + 1500;
	float fyMediaReciptType1 = fyMediaType1;
	this->HDDrawText(szMediaType, *hdcPrint, fxUser1, fyMediaReciptType1,80);//外发


	//介质密级   右上角
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_ReceiptJobInfo.nSeclv, szSecString); 

	//float fxLevel1 = fxReceiveGroup1 + 3200.0;
	//float fyLevel1 = fyMediaType1;
	this->HDDrawText(szSecString, *hdcPrint, fxSubmitType, fyMediaReciptType1,80); //非密

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	float fyCount = fyMediaType1 + 132.5;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxSubmitType, fyCount,80);

	//介质详细信息
	float fyMedia = fyMediaType1 + 310;

	float fxMediaCode1 = fxSendGroup1+720;
	float fxMediaCode = fySendGroup1 - 220;
	float fxMediaName = fxMediaCode + 1100;	
	float fxMediaLevel = fxMediaName + 1750;
	float fxMediaNum = fxMediaLevel + 420;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 275;
	float fyMediaNameEx = fyMediaType1 + 310;
	char szTempFileName[MAX_PATH] = {0x00};

	int nFeimi = 0;
	int nNeibu = 0;
	int nPutongshangmi = 0;
	int nMimi = 0;
	int nJimi = 0;
	int nHexinshangmi = 0;
	int nTotalCount = 0;

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		GenLog(DEBUG_INFO, "%s[%d].交接单CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName:%s\n",__FILE__,__LINE__, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		//WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);
		int nFlags = HDAppConfig::Instance()->m_ExConfig.m_hdnservertype;

		GenLog(DEBUG_INFO, "%s[%d].交接单m_hdnservertype:%d\n",__FILE__,__LINE__, nFlags);
		/*if(nFlags==1)
		{
		int nwlen = ::MultiByteToWideChar(CP_UTF8,0,szTempFileName,-1,NULL,0);
		wchar_t * pwbuf=new wchar_t[nwlen+1];
		memset (pwbuf,0,nwlen*2+2);
		::MultiByteToWideChar(CP_UTF8,0,szTempFileName,strlen(szTempFileName),pwbuf,nwlen);
		int nlen= ::WideCharToMultiByte(CP_ACP,0,pwbuf,-1,NULL,NULL,NULL,NULL);
		char * pbuf=new char[nlen+1];
		memset(pbuf,0,nlen+1);
		::WideCharToMultiByte(CP_ACP,0,pwbuf,nwlen,pbuf,nlen,NULL,NULL);
		strcpy(szTempFileName,pbuf);
		GenLog(DEBUG_INFO,"%s[%d].国产化转码[%s]\n", __FILE__, __LINE__,szTempFileName);
		}
		else
		{*/

			MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
			WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);
			strcpy(szTempFileName,temp);
		//}
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		//屏蔽载体名称

		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szTempFileName);
		if (strlen(szTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
				strncpy(szExFileName, szTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}
		
		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode1, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		/*if(strcmp(szLevelString, "非密") == 0)
		{
		nFeimi++;
		}
		if(strcmp(szLevelString, "内部") == 0)
		{
		nNeibu++;
		}
		if(strcmp(szLevelString, "普通商密") == 0)
		{
		nPutongshangmi++;
		}
		if(strcmp(szLevelString, "秘密") == 0)
		{
		nMimi++;
		}
		if(strcmp(szLevelString, "机密") == 0)
		{
		nJimi++;
		}
		if(strcmp(szLevelString, "核心商密") == 0)
		{
		nHexinshangmi++;
		}*/

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 190;
		fyMediaName += 190;
		fyMediaNameEx += 190;

	}

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 2940;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	// 接受单位
	float fxSendGroup2 = fxCode2;
	float fySendGroup2 = fyCode2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxSendGroup2, fySendGroup2, 80);// RECEIPT_307定时


	// 接受人
	float fxSendUser2 = fxCode2 + 2700; 
	float fySender2 = fyCode2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxSendUser2, fySender2, 80);//啊最深的

	// 部门/单位
	float fxUserDept2  = fxSendUser2;
	float fyUserDept2  = fyCode2+130;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxUserDept2, fyUserDept2, 80);// 测试测试

	//申请时间
	//申请时间
	GenLog(DEBUG_INFO, "%s[%d].交接单作业申请时间为：[%s]\n",__FILE__,__LINE__,  pJobinfo->m_ReceiptJobInfo.szAppTime);	
	char apptime[64]={0};
	int year,month,day;
	sscanf(  pJobinfo->m_ReceiptJobInfo.szAppTime,"%d-%d-%d",&year,&month,&day);
	sprintf(apptime,"%d年%d月%d日",year,month,day);	
	GenLog(DEBUG_INFO, "%s[%d].交接单作业申请时间为：[%s]\n",__FILE__,__LINE__,apptime);
	// 申请时间显示
	float fxSendYearTime =fxCode2 + 1200;
	float fySendYearTime = fySendGroup1;
	this->HDDrawText(apptime, *hdcPrint, fxSendYearTime, fyUserDept2, 80); 

	// 申请人
	//float fxSender2 = fxCode2 + 2700;
	float fxSender2 = fxCode2;
	 fySender2 = fyCode2+130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender2, fySender2, 80);// yy1

	//报送单位
	this->HDDrawText(m_HDAppConfig->m_ExConfig.m_strGroupName, *hdcPrint, fxCode2, fySender2 + 130,80);

	//报送方式 
	 fxSubmitType = fxSendUser2 +360; 
	 fySubmitType = fySender2 + 130;
	if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 1)
	{
		this->HDDrawText("专送", *hdcPrint,fxSubmitType, fySubmitType,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 0)
	{
		this->HDDrawText("机要", *hdcPrint, fxSubmitType, fySubmitType,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 2)
	{
		this->HDDrawText("邮政EMS", *hdcPrint, fxSubmitType, fySubmitType,80);
	}
	else
	{
		this->HDDrawText("无", *hdcPrint, fxSubmitType, fySubmitType,80);
	}
	//密级
	this->HDDrawText(szSecString, *hdcPrint, fxSubmitType, fySender2 +260,80); //非密

	// 载体类型
	float fxMediaType2 = fxCode2;
	float fyMediaType2 = fySender2 +260;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2, 80);

	// 交接单类型
	float fxMediaReciptType2 = fxCode2 + 2000;
	float fyMediaReciptType2 = fyMediaType2;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType2, fyMediaReciptType2, 80);


	//显示统计	
	
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxSubmitType,  fySender2 +700,80);

	strCount.ReleaseBuffer();
	//显示统计	
	//float fyCount2 = fyMediaReciptType2 + 200;
	//this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	//strCount.ReleaseBuffer();

	//显示材料名称标题	
	//float fyFileName2 = fyMediaReciptType2 + 200;
	//this->HDDrawText(strFileName.GetBuffer(0), *hdcPrint, fxFileName, fyFileName2,80);
	//strFileName.ReleaseBuffer();

	//介质详细信息
	fyMedia = fyMediaReciptType2 + 410;

	fyMedia = fyMediaType2 + 600;

	//fxMediaCode1 = fxReceiveGroup2 +720;// - 220;

	fxMediaCode = fxCode2  - 220;
	fxMediaName = fxMediaCode + 1100;	
	fxMediaLevel = fxMediaName + 1750;
	fxMediaNum = fxMediaLevel + 420;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 600;
	float fyDownMediaNameEx = fyMediaType2 + 650;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);
		GenLog(DEBUG_INFO, "%s[%d].交接单CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName:%s\n",__FILE__,__LINE__, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szDownTempFileName);
		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szDownTempFileName);
		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		int nFlags = HDAppConfig::Instance()->m_ExConfig.m_hdnservertype;

		/*GenLog(DEBUG_INFO, "%s[%d].交接单m_hdnservertype:%d\n",__FILE__,__LINE__, nFlags);
		if(nFlags==1)
		{
		int nwlen = ::MultiByteToWideChar(CP_UTF8,0,szDownTempFileName,-1,NULL,0);
		wchar_t * pwbuf=new wchar_t[nwlen+1];
		memset (pwbuf,0,nwlen*2+2);
		::MultiByteToWideChar(CP_UTF8,0,szDownTempFileName,strlen(szDownTempFileName),pwbuf,nwlen);
		int nlen= ::WideCharToMultiByte(CP_ACP,0,pwbuf,-1,NULL,NULL,NULL,NULL);
		char * pbuf=new char[nlen+1];
		memset(pbuf,0,nlen+1);
		::WideCharToMultiByte(CP_ACP,0,pwbuf,nwlen,pbuf,nlen,NULL,NULL);
		strcpy(szDownTempFileName,pbuf);
		GenLog(DEBUG_INFO,"%s[%d].国产化转码[%s]\n", __FILE__, __LINE__,szDownTempFileName);
		}
		else
		{*/

				MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
				WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);
				strcpy(szDownTempFileName,temp);
		//}
		GenLog(DEBUG_INFO, "%s[%d].交接单szTempFileName:%s\n",__FILE__,__LINE__, szDownTempFileName);
		//屏蔽载体名称
		if (strlen(szDownTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szDownTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szDownTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
			TCHAR szExFileName[60] = {0x00};
			if(strlen(szDownTempFileName) > 80)
			{
				strncpy(szExFileName, szDownTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szDownTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szDownTempFileName);

			GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

		}
		
		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode1, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 190;
		fyDownMediaName += 190;
		fyDownMediaNameEx += 190;
	}

	return 0;
}


int CHDPrinter::Print3buReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);

	// 部门/单位
	float fxUserDept1  = fxCode1;// + 250;
	float fyUserDept1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxUserDept1, fyUserDept1, 80);

	// 申请人
	float fxUser1  = fxCode1 + 2700;// + 250;
	float fyUser1  = fyUserDept1;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxUser1, fyUser1, 80);

	// 接受单位
	float fxSendGroup1 = fxCode1+1400;
	float fySendGroup1 = fyUser1 + 130;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxSendGroup1, fySendGroup1, 80);

	// 接受人
	float fxSendUser1 = fxCode1 + 2700; 
	float fySendUser1 = fySendGroup1;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSendUser1, fySendUser1, 80);

	// 载体类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxCode1;
	float fyMediaType1 = fySendUser1 + 180;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1, 80);

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	float fxMediaReciptType1 = fxCode1 + 1500;
	float fyMediaReciptType1 = fyMediaType1;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType1, fyMediaReciptType1,80);

	//显示统计	
	//CString strCount;
	////刻录交接单
	//if (strcmp(szReciptType, "光盘") == 0) 
	//{			
	//	strCount.Format(_T("光盘张数"));
	//}
	//else
	//{
	//	strCount.Format(_T("文件页数"));
	//}
	//float fxCount = fxCode1 + 2750;
	//float fyCount = fyMediaReciptType1 + 250;
	//this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	//strCount.ReleaseBuffer();

	////显示材料名称标题	
	//CString strFileName;
	////刻录交接单
	//if (strcmp(szReciptType, "光盘") == 0) 
	//{			
	//	strFileName.Format(_T("光盘内容"));
	//}
	//else
	//{
	//	strFileName.Format(_T("文件名称"));
	//}
	//float fxFileName = fxCode1 + 1500;
	//float fyFileName = fyMediaReciptType1 + 250;
	//this->HDDrawText(strFileName.GetBuffer(0), *hdcPrint, fxFileName, fyFileName,80);
	//strFileName.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaReciptType1 + 500-10;

	float fxMediaCode = fxCode1 - 220+650;
	//float fxMediaName = fxMediaCode + 1100;	
	float fxMediaLevel = fxMediaCode +1700;
	float fxMediaNum = fxMediaLevel + 470;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 490;
	float fyMediaNameEx = fyMediaType1 + 575;
	char szTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);
		if (strlen(szTempFileName) > 32)
		{
			fyMediaName -= 80;
			fyMediaNameEx -= 80;
		}
		if (strlen(szTempFileName) > 32)
		{

			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<32; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 30)
					{
						break;
					}
					nLen++;
				}
			}

			/*strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
			strncpy(szExFileName, szTempFileName + nLen, 32);
			this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
			strcpy(szExFileName, szTempFileName + nLen);
			this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}*/
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();
		if (strlen(szTempFileName) > 32)
		{
			fyMediaName += 80;
			fyMediaNameEx+= 80;
		}
		fyMedia += 190;
		fyMediaName += 190;
		fyMediaNameEx += 190;
	}

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 2910+260;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	// 部门/单位
	float fxUserDept2  = fxCode2;
	float fyUserDept2  = fyCode2;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxUserDept2, fyUserDept2, 80);

	//申请时间
	//申请时间
	GenLog(DEBUG_INFO, "%s[%d].交接单作业申请时间为：[%s]\n",__FILE__,__LINE__,  pJobinfo->m_ReceiptJobInfo.szAppTime);	
	char apptime[64]={0};
	int year,month,day;
	sscanf(  pJobinfo->m_ReceiptJobInfo.szAppTime,"%d/%d/%d",&year,&month,&day);
	sprintf(apptime,"%d年%d月%d日",year,month,day);	
	GenLog(DEBUG_INFO, "%s[%d].交接单作业申请时间为：[%s]\n",__FILE__,__LINE__,apptime);
	// 申请时间显示
	float fxSendYearTime =fxCode2 + 1300;
	float fySendYearTime = fySendGroup1;
	this->HDDrawText(apptime, *hdcPrint, fxSendYearTime, fyUserDept2, 80);

	// 申请人
	float fxSender2 = fxCode2 + 2700;
	float fySender2 = fyCode2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender2, fySender2, 80);

	// 接受单位
	float fxSendGroup2 = fxCode2;
	float fySendGroup2 = fySender2 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxSendGroup2, fySendGroup2, 80);

	// 接受人
	float fxSendUser2 = fxCode2 + 2700; 
	float fySendUser2 = fySendGroup2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxSendUser2, fySendUser2, 80);

	// 载体类型
	float fxMediaType2 = fxCode2;
	float fyMediaType2 = fySendUser2 + 230-10;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2, 80);

	// 交接单类型
	float fxMediaReciptType2 = fxCode2 + 1500.0;
	float fyMediaReciptType2 = fyMediaType2;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType2, fyMediaReciptType2, 80);

	//显示统计	
	//float fyCount2 = fyMediaReciptType2 + 200;
	//this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	//strCount.ReleaseBuffer();

	//显示材料名称标题	
	//float fyFileName2 = fyMediaReciptType2 + 200;
	//this->HDDrawText(strFileName.GetBuffer(0), *hdcPrint, fxFileName, fyFileName2,80);
	//strFileName.ReleaseBuffer();

	//介质详细信息
	fyMedia = fyMediaReciptType2 + 410;

	fxMediaCode = fxCode2 - 210+650;
	//fxMediaName = fxMediaCode + 1150;	
	fxMediaLevel = fxMediaCode + 1700;
	fxMediaNum = fxMediaLevel + 470;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 420;
	float fyDownMediaNameEx = fyMediaType2 + 500;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);
		if (strlen(szDownTempFileName) > 32)
		{
			fyDownMediaName -= 90;
			fyDownMediaNameEx -= 90;
		}
		if (strlen(szDownTempFileName) > 32)
		{
			CString strFileName;
			strFileName.Format("%s",szDownTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<32; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 30)
					{
						break;
					}
					nLen++;
				}
			}

			/*strncpy(szFileName, szDownTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szDownTempFileName) > 80)
			{
			strncpy(szExFileName, szDownTempFileName + nLen, 32);
			this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
			else
			{
			strcpy(szExFileName, szDownTempFileName + nLen);
			this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}*/
		}
		else
		{
			strcpy(szFileName, szDownTempFileName);

			GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
		//	this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();
		if (strlen(szDownTempFileName) > 32)
		{
			fyDownMediaName += 90;
			fyDownMediaNameEx+= 90;
		}
		fyMedia += 190;
		fyDownMediaName += 190;
		fyDownMediaNameEx += 190;
	}

	return 0;
}


// 716交接单模板
int CHDPrinter::Print716ReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	GenLog(DEBUG_INFO,"%s[%d].（ReceiptTaskInfo结构体）：交接单报送方式:%d \n", __FILE__, __LINE__, pJobinfo->m_ReceiptJobInfo.iSubmitType);
	// 统计交接单总数 [1/24/2018 Administrator]
	int nFeimi = 0;
	int nNeibu = 0;
	int nPutongshangmi = 0;
	int nMimi = 0;
	int nJimi = 0;
	int nHexinshangmi = 0;
	int nTotalCount = 0;

	for (int i = 0; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()); i++)
	{

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		//	this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		if(strcmp(szLevelString, "非密") == 0)
		{
			nFeimi++;
		}
		if(strcmp(szLevelString, "内部") == 0)
		{
			nNeibu++;
		}
		if(strcmp(szLevelString, "普通商密") == 0)
		{
			nPutongshangmi++;
		}
		if(strcmp(szLevelString, "秘密") == 0)
		{
			nMimi++;
		}
		if(strcmp(szLevelString, "机密") == 0)
		{
			nJimi++;
		}
		if(strcmp(szLevelString, "核心商密") == 0)
		{
			nHexinshangmi++;
		}


	}

	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);

	//接收单位
	float fxReceiveGroup1  = fxCode1;// + 250;
	float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup1, fyReceiveGroup1,80);

	// 接收人
	float fxReceiverGroup1  = fxCode1 + 2200;// + 250;
	//float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxReceiverGroup1, fyReceiveGroup1, 80);


	//报送单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s %s"), m_HDAppConfig->m_ExConfig.m_strGroupName, pJobinfo->m_ReceiptJobInfo.szUserName);

	float fxSendGroup1 = fxReceiveGroup1;
	float fySendGroup1 = fyReceiveGroup1 + 130;
	this->HDDrawText(strSendGroup.GetBuffer(0), *hdcPrint, fxSendGroup1, fySendGroup1, 80);
	strSendGroup.ReleaseBuffer();


	//报送方式
	float fxSubmit = fxReceiveGroup1 + 3200.0;
	float fySubmit = fySendGroup1;
	if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 1)
	{
		this->HDDrawText("专送", *hdcPrint, fxSubmit, fySubmit,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 0)
	{
		this->HDDrawText("机要", *hdcPrint, fxSubmit, fySubmit,80);
	}
	else
	{
		this->HDDrawText("无", *hdcPrint, fxSubmit, fySubmit,80);
	}

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxReceiveGroup1;
	float fyMediaType1 = fySendGroup1 + 132.5;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1,80);

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	float fxMediaReciptType1 = fxReceiveGroup1 + 2000.0;
	float fyMediaReciptType1 = fySendGroup1 + 132.5;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType1, fyMediaReciptType1,80);

	//介质密级
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_ReceiptJobInfo.nSeclv, szSecString);

	float fxLevel1 = fxReceiveGroup1 + 3200.0;
	float fyLevel1 = fyMediaType1;
	this->HDDrawText(szSecString, *hdcPrint, fxLevel1, fyLevel1,80);

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	float fyCount = fyMediaType1 + 132.5;
	float fxCount = fxReceiveGroup1 + 3050;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaType1 + 310;

	float fxMediaCode = fxReceiveGroup1 - 220;
	float fxMediaName = fxMediaCode + 1100;	
	float fxMediaLevel = fxMediaName + 1750;
	float fxMediaNum = fxMediaLevel + 420;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 275;
	float fyMediaNameEx = fyMediaType1 + 375;
	char szTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);

		if (strlen(szTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
				strncpy(szExFileName, szTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyMediaName += 270;
		fyMediaNameEx += 270;

	}

	// 载体总份数，各个密级份数、份数
	float fxJobCount1 = fxCode1 + 2000;// + 250;
	float fyJobCount1 = fyCode1 + 185 - 210 - 132*3;
	float fyEcohSecTypeOne1  = fyCode1 + 185 - 210 - 132*2;
	float fyEcohSecTypeTwo1  = fyCode1 + 185 - 210 - 132*1;

	nTotalCount = nFeimi + nNeibu + nMimi + nJimi + nPutongshangmi + nHexinshangmi;
	CString csTotalCount;
	csTotalCount.Format("本次外发共计 %d 份", nTotalCount);
	CString csEcohSecTypeOne;
	csEcohSecTypeOne.Format("非密: %d 份;内部: %d 份;普通商密: %d 份", nFeimi, nNeibu, nPutongshangmi);
	CString csEcohSecTypeTwo;
	csEcohSecTypeTwo.Format("秘密: %d 份;机密: %d 份;核心商密: %d 份", nMimi, nJimi, nHexinshangmi);

	this->HDDrawText(csTotalCount.GetBuffer(), *hdcPrint, fxJobCount1, fyJobCount1,60);
	this->HDDrawText(csEcohSecTypeOne.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeOne1,60);
	this->HDDrawText(csEcohSecTypeTwo.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeTwo1,60);

	float fyJobCount2 = fyCode1 + 3300 - 257 + 200 - 132*3;
	float fyEcohSecTypeOne2  = fyCode1 + 3300 - 257 + 200 - 132*2;
	float fyEcohSecTypeTwo2  = fyCode1 + 3300 - 257 + 200 - 132*1;
	this->HDDrawText(csTotalCount.GetBuffer(), *hdcPrint, fxJobCount1, fyJobCount2, 60);
	this->HDDrawText(csEcohSecTypeOne.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeOne2, 60);
	this->HDDrawText(csEcohSecTypeTwo.GetBuffer(), *hdcPrint, fxJobCount1, fyEcohSecTypeTwo2, 60);

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 3300 - 257;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	//接收单位
	float fxReceiveGroup2  = fxCode2;// + 300;
	float fyReceiveGroup2  = fyCode2 + 200;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup2, fyReceiveGroup2,80);

	// 接收人
	float fxReceiverGroup2  = fxCode2 + 2200;// + 250;
	//float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxReceiverGroup2, fyReceiveGroup2, 80);

	//申请人
	float fxSender = fxReceiveGroup2;
	float fySender = fyReceiveGroup2 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender, fySender, 80);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	//申请时间
	float fxSendTime = fxSender + 1100;//1175;
	float fySendTime = fySender;
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(18);
	this->HDDrawText(strTime.GetBuffer(0), *hdcPrint, fxSendTime, fySendTime, 80);
	strTime.ReleaseBuffer();

	//申请部门
	float fxSendGroup = fxSendTime + 1450;
	float fySendGroup = fySender;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxSendGroup, fySendGroup, 80);

	//报送单位
	float fxSendGroup2 = fxReceiveGroup2;
	float fySendGroup2 = fySender + 130;
	this->HDDrawText(m_HDAppConfig->m_ExConfig.m_strGroupName, *hdcPrint, fxSendGroup2, fySendGroup2,80);

	//报送方式
	float fxSubmit2 = fxReceiveGroup2 + 3200.0;
	float fySubmit2 = fySendGroup2;
	if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 1)
	{
		this->HDDrawText("专送", *hdcPrint, fxSubmit2, fySubmit2,80);
	}
	else if(pJobinfo->m_ReceiptJobInfo.iSubmitType == 0)
	{
		this->HDDrawText("机要", *hdcPrint, fxSubmit2, fySubmit2,80);
	}
	else
	{
		this->HDDrawText("无", *hdcPrint, fxSubmit2, fySubmit2,80);
	}

	//介质类型
	float fxMediaType2 = fxReceiveGroup2;
	float fyMediaType2 = fySendGroup2 + 130;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2,80);

	//交接单类型
	float fxMediaReciptType2 = fxReceiveGroup2 + 2000.0;
	float fyMediaReciptType2 = fySendGroup2 + 130;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType2, fyMediaReciptType2,80);

	//介质密级
	float fxLevel2 = fxMediaType2 + 3200.0;
	float fyLevel2 = fyMediaType2;
	this->HDDrawText(szSecString, *hdcPrint, fxLevel2, fyLevel2, 80);

	//显示统计	
	float fyCount2 = fyMediaType2 + 132.5;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	strCount.ReleaseBuffer();
	//介质详细信息
	fyMedia = fyMediaType2 + 310;

	fxMediaCode = fxReceiveGroup2 - 220;
	fxMediaName = fxMediaCode + 1100;	
	fxMediaLevel = fxMediaName + 1750;
	fxMediaNum = fxMediaLevel + 420;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 275;
	float fyDownMediaNameEx = fyMediaType2 + 375;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);

		if (strlen(szDownTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szDownTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szDownTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szDownTempFileName) > 80)
			{
				strncpy(szExFileName, szDownTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szDownTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szDownTempFileName);

			GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyDownMediaName += 270;
		fyDownMediaNameEx += 270;
	}

	return 0;
}


int CHDPrinter::Print13ReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);

	// 部门/单位
	float fxUserDept1  = fxCode1;// + 250;
	float fyUserDept1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxUserDept1, fyUserDept1, 80);

	// 申请人
	float fxUser1  = fxCode1 + 1500;// + 250;
	float fyUser1  = fyUserDept1;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxUser1, fyUser1, 80);

	// 接受单位
	float fxSendGroup1 = fxCode1;
	float fySendGroup1 = fyUser1 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxSendGroup1, fySendGroup1, 80);

	// 接受人
	float fxSendUser1 = fxCode1 + 2800; 
	float fySendUser1 = fySendGroup1;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxSendUser1, fySendUser1, 80);

	// 载体类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxCode1;
	float fyMediaType1 = fySendUser1 + 180;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1, 80);

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	float fxMediaReciptType1 = fxCode1 + 1500;
	float fyMediaReciptType1 = fyMediaType1;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType1, fyMediaReciptType1,80);

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘张数"));
	}
	else
	{
		strCount.Format(_T("文件页数"));
	}
	float fxCount = fxCode1 + 2750;
	float fyCount = fyMediaReciptType1 + 250;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//显示材料名称标题	
	CString strFileName;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strFileName.Format(_T("光盘内容"));
	}
	else
	{
		strFileName.Format(_T("文件名称"));
	}
	float fxFileName = fxCode1 + 1500;
	float fyFileName = fyMediaReciptType1 + 250;
	this->HDDrawText(strFileName.GetBuffer(0), *hdcPrint, fxFileName, fyFileName,80);
	strFileName.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaReciptType1 + 500;

	float fxMediaCode = fxCode1 - 220;
	float fxMediaName = fxMediaCode + 1100;	
	float fxMediaLevel = fxMediaName + 1550;
	float fxMediaNum = fxMediaLevel + 320;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 475;
	float fyMediaNameEx = fyMediaType1 + 575;
	char szTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<3); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);

		if (strlen(szTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
				strncpy(szExFileName, szTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyMediaName += 270;
		fyMediaNameEx += 270;
	}

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 3100;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	// 部门/单位
	float fxUserDept2  = fxCode2;
	float fyUserDept2  = fyCode2;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxUserDept2, fyUserDept2, 80);

	// 申请人
	float fxSender2 = fxCode2 + 1500;
	float fySender2 = fyCode2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender2, fySender2, 80);

	// 接受单位
	float fxSendGroup2 = fxCode2;
	float fySendGroup2 = fySender2 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxSendGroup2, fySendGroup2, 80);

	// 接受人
	float fxSendUser2 = fxCode2 + 2800; 
	float fySendUser2 = fySendGroup2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxSendUser2, fySendUser2, 80);

	// 载体类型
	float fxMediaType2 = fxCode2;
	float fyMediaType2 = fySendUser2 + 230;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2, 80);

	// 交接单类型
	float fxMediaReciptType2 = fxCode2 + 1500.0;
	float fyMediaReciptType2 = fyMediaType2;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType2, fyMediaReciptType2, 80);

	//显示统计	
	float fyCount2 = fyMediaReciptType2 + 300;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	strCount.ReleaseBuffer();

	//显示材料名称标题	
	float fyFileName2 = fyMediaReciptType2 + 300;
	this->HDDrawText(strFileName.GetBuffer(0), *hdcPrint, fxFileName, fyFileName2,80);
	strFileName.ReleaseBuffer();

	//介质详细信息
	fyMedia = fyMediaReciptType2 + 500;

	fxMediaCode = fxCode2 - 220;
	fxMediaName = fxMediaCode + 1100;	
	fxMediaLevel = fxMediaName + 1550;
	fxMediaNum = fxMediaLevel + 320;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 475;
	float fyDownMediaNameEx = fyMediaType2 + 575;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 3); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);

		if (strlen(szDownTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szDownTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szDownTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szDownTempFileName) > 80)
			{
				strncpy(szExFileName, szDownTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szDownTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szDownTempFileName);

			GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyDownMediaName += 270;
		fyDownMediaNameEx += 270;
	}

	return 0;
}


int CHDPrinter::PrintReceiptCarryOutText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);

	// 申请人
	float fxUser1  = fxCode1;// + 250;
	float fyUser1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxUser1, fyUser1,80);

	// 申请单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s"), pJobinfo->m_PrintJobInfo.szGroupName);

	float fxDept1 = fxCode1 + 2300;
	float fyDept1 = fyUser1;
	this->HDDrawText(strSendGroup.GetBuffer(0), *hdcPrint, fxDept1, fyDept1, 80);
	strSendGroup.ReleaseBuffer();

	// 携带人，数据中接收人
	float fxCarryOutUser1 = fxCode1;
	float fyCarryOutUser1 = fyDept1 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxCarryOutUser1, fyCarryOutUser1, 80);

	// 携带部门，数据中接受部门
	float fxCarryOutDept1 = fxCode1 + 2300;
	float fyCarryOutDept1 = fyCarryOutUser1;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxCarryOutDept1, fyCarryOutDept1, 80);

	// 载体类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	float fxMediaType1 = fxCode1;
	float fyMediaType1 = fyCarryOutDept1 + 132.5;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1,80);

	// 显示统计	
	CString strCount;

	// 外带单统计
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}

	float fxCount = fxCode1 + 3050;
	float fyCount = fyMediaType1 + 132.5;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaType1 + 310;

	float fxMediaCode = fxCode1 - 220;
	float fxMediaName = fxMediaCode + 1100;	
	float fxMediaLevel = fxMediaName + 1750;
	float fxMediaNum = fxMediaLevel + 420;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 275;
	float fyMediaNameEx = fyMediaType1 + 375;
	char szTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);

		if (strlen(szTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szTempFileName) > 80)
			{
				strncpy(szExFileName, szTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szTempFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		// 刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyMediaName += 270;
		fyMediaNameEx += 270;

	}

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 3300 - 257;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	// 申请人
	float fxUser2  = fxCode2;
	float fyUser2  = fyCode2 + 200;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxUser2, fyUser2,80);

	// 申请部门
	float fxDept2 = fxCode2 + 2300;
	float fyDept2 = fyUser2;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxDept2, fyDept2, 80);

	// 携带人，数据中接收人
	float fxCarryOutUser2 = fxCode2;
	float fyCarryOutUser2 = fyDept2 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiver, *hdcPrint, fxCarryOutUser2, fyCarryOutUser2, 80);

	// 携带部门，数据中接受部门
	float fxCarryOutDept2 = fxCode2 + 2300;
	float fyCarryOutDept2 = fyCarryOutUser2;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxCarryOutDept2, fyCarryOutDept2, 80);

	// 载体类型
	float fxMediaType2 = fxCode2;
	float fyMediaType2 = fyCarryOutDept2 + 130;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2, 80);

	//显示统计	
	float fyCount2 = fyMediaType2 + 132.5;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	strCount.ReleaseBuffer();
	//介质详细信息
	fyMedia = fyMediaType2 + 310;

	fxMediaCode = fxCode2 - 220;
	fxMediaName = fxMediaCode + 1100;	
	fxMediaLevel = fxMediaName + 1750;
	fxMediaNum = fxMediaLevel + 420;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 275;
	float fyDownMediaNameEx = fyMediaType2 + 375;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);

		if (strlen(szDownTempFileName) > 40)
		{
			CString strFileName;
			strFileName.Format("%s",szDownTempFileName);
			int nLen = 0;

			for (nLen=0; nLen<40; nLen++)
			{
				TCHAR szTmp1;
				szTmp1 = strFileName.GetAt(nLen);
				if (szTmp1 < 0)
				{
					if (nLen > 38)
					{
						break;
					}
					nLen++;
				}
			}

			strncpy(szFileName, szDownTempFileName, nLen);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

			TCHAR szExFileName[60] = {0x00};
			if(strlen(szDownTempFileName) > 80)
			{
				strncpy(szExFileName, szDownTempFileName + nLen, 40);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
			else
			{
				strcpy(szExFileName, szDownTempFileName + nLen);
				this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
			}
		}
		else
		{
			strcpy(szFileName, szDownTempFileName);

			GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
			this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
		}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyDownMediaName += 270;
		fyDownMediaNameEx += 270;
	}

	return 0;
}


int CHDPrinter::Print7suoReceiptText(PrintJob* pJobinfo, HDC *hdcPrint, int nNum)
{
	//上联
	//打印记录单号
	float fxCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETX) - 50;
	float fyCode1 = 1050.0 - GetDeviceCaps(*hdcPrint, PHYSICALOFFSETY) - 150;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode1, fyCode1, 80);

	//接收单位
	float fxReceiveGroup1  = fxCode1;// + 250;
	float fyReceiveGroup1  = fyCode1 + 185 - 210;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup1, fyReceiveGroup1,80);

	//报送单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s %s"), m_HDAppConfig->m_ExConfig.m_strGroupName, pJobinfo->m_ReceiptJobInfo.szUserName);

	float fxSendGroup1 = fxReceiveGroup1;
	float fySendGroup1 = fyReceiveGroup1 + 130;
	this->HDDrawText(strSendGroup.GetBuffer(0), *hdcPrint, fxSendGroup1, fySendGroup1, 80);
	strSendGroup.ReleaseBuffer();

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	//CString strMediaType;
	//if ((strcmp(pJobinfo->m_ReceiptJobInfo.szJobTypeCode, "PRINT_SEND") == 0) || (strcmp(pJobinfo->m_ReceiptJobInfo.szJobTypeCode, "SEND_PAPER") == 0))
	//{
	//	strMediaType.Format(_T("纸质"));
	//}
	//else if ((strcmp(pJobinfo->m_ReceiptJobInfo.szJobTypeCode, "BURN_SEND") == 0) || (strcmp(pJobinfo->m_ReceiptJobInfo.szJobTypeCode, "SEND_CD") == 0)) 
	//{
	//	strMediaType.Format(_T("光盘"));
	//}
	//else if (strcmp(pJobinfo->m_ReceiptJobInfo.szJobTypeCode, "SEND") == 0)
	//{
	//	strMediaType.Format(_T("外发"));
	//}
	//else
	//{
	//	strMediaType.Format(_T("外发"));
	//}

	float fxMediaType1 = fxReceiveGroup1;
	float fyMediaType1 = fySendGroup1 + 132.5;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType1, fyMediaType1,80);

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	float fxMediaReciptType1 = fxReceiveGroup1 + 2000.0;
	float fyMediaReciptType1 = fySendGroup1 + 132.5;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType1, fyMediaReciptType1,80);

	//介质密级
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_ReceiptJobInfo.nSeclv, szSecString);

	float fxLevel1 = fxReceiveGroup1 + 3000.0;
	float fyLevel1 = fyMediaType1;
	this->HDDrawText(szSecString, *hdcPrint, fxLevel1, fyLevel1,80);

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	float fyCount = fyMediaType1 + 132.5;
	float fxCount = fxReceiveGroup1 + 3050;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount,80);
	strCount.ReleaseBuffer();

	//介质详细信息
	float fyMedia = fyMediaType1 + 310;

	//float fxMediaCode = fxReceiveGroup1 - 220;
	//float fxMediaName = fxMediaCode + 1100;	
	float fxMediaCode = fxReceiveGroup1 + 720;
	float fxMediaName = fxMediaCode + 200;	
	float fxMediaLevel = fxMediaName + 1750;
	float fxMediaNum = fxMediaLevel + 420;
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyMediaName = fyMediaType1 + 275;
	float fyMediaNameEx = fyMediaType1 + 375;
	char szTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		//memset(szFileName, 0x00, sizeof(szFileName));

		//strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		////去除字符串中的空格   [郝佳 2015-6-2]
		//CString strFileName;
		//strFileName.Format("%s",szTempFileName);
		//strFileName.TrimLeft();
		//strFileName.TrimRight();
		//strFileName.Trim();

		//memset(szTempFileName, 0x00, sizeof(szTempFileName));
		//strcpy(szTempFileName,strFileName);

		////判断最后一位是否为中文   [郝佳 2015-6-2]

		//DWORD dwText[512] = {0x00};
		//char temp[1024] = {0x00};

		//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		//WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		//strcpy(szTempFileName,temp);

		//if (strlen(szTempFileName) > 40)
		//{
		//	CString strFileName;
		//	strFileName.Format("%s",szTempFileName);
		//	int nLen = 0;

		//	for (nLen=0; nLen<40; nLen++)
		//	{
		//		TCHAR szTmp1;
		//		szTmp1 = strFileName.GetAt(nLen);
		//		if (szTmp1 < 0)
		//		{
		//			if (nLen > 38)
		//			{
		//				break;
		//			}
		//			nLen++;
		//		}
		//	}

		//	strncpy(szFileName, szTempFileName, nLen);
		//	this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);

		//	TCHAR szExFileName[60] = {0x00};
		//	if(strlen(szTempFileName) > 80)
		//	{
		//		strncpy(szExFileName, szTempFileName + nLen, 40);
		//		this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
		//	}
		//	else
		//	{
		//		strcpy(szExFileName, szTempFileName + nLen);
		//		this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyMediaNameEx, 80);
		//	}
		//}
		//else
		//{
		//	strcpy(szFileName, szTempFileName);
		//	this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMediaName, 80);
		//}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		/*this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);*/

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyMediaName += 270;
		fyMediaNameEx += 270;

	}

	//下联
	//打印记录单号
	float fxCode2 = fxCode1;
	float fyCode2 = fyCode1 + 3300 - 257;
	// 屏蔽记录单号 [3/19/2015 chenhong]
	//this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szJobCode, *hdcPrint, fxCode2, fyCode2,80);

	//接收单位
	float fxReceiveGroup2  = fxCode2;// + 300;
	float fyReceiveGroup2  = fyCode2 + 200;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szReceiveDept, *hdcPrint, fxReceiveGroup2, fyReceiveGroup2,80);

	//申请人
	float fxSender = fxReceiveGroup2;
	float fySender = fyReceiveGroup2 + 130;
	this->HDDrawText(pJobinfo->m_ReceiptJobInfo.szUserName, *hdcPrint, fxSender, fySender, 80);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	//申请时间
	float fxSendTime = fxSender + 1100;//1175;
	float fySendTime = fySender;
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(18);
	this->HDDrawText(strTime.GetBuffer(0), *hdcPrint, fxSendTime, fySendTime, 80);
	strTime.ReleaseBuffer();

	//申请部门
	float fxSendGroup = fxSendTime + 1450;
	float fySendGroup = fySender;
	this->HDDrawText(pJobinfo->m_PrintJobInfo.szGroupName, *hdcPrint, fxSendGroup, fySendGroup, 80);

	//报送单位
	float fxSendGroup2 = fxReceiveGroup2;
	float fySendGroup2 = fySender + 130;
	this->HDDrawText(m_HDAppConfig->m_ExConfig.m_strGroupName, *hdcPrint, fxSendGroup2, fySendGroup2,80);

	//介质类型
	float fxMediaType2 = fxReceiveGroup2;
	float fyMediaType2 = fySendGroup2 + 130;
	this->HDDrawText(szReciptType, *hdcPrint, fxMediaType2, fyMediaType2,80);

	//交接单类型
	float fxMediaReciptType2 = fxReceiveGroup2 + 2000.0;
	float fyMediaReciptType2 = fySendGroup2 + 130;
	this->HDDrawText(szMediaType, *hdcPrint, fxMediaReciptType2, fyMediaReciptType2,80);

	//介质密级
	float fxLevel2 = fxMediaType2 + 3000.0;
	float fyLevel2 = fyMediaType2;
	this->HDDrawText(szSecString, *hdcPrint, fxLevel2, fyLevel2, 80);

	//显示统计	
	float fyCount2 = fyMediaType2 + 132.5;
	this->HDDrawText(strCount.GetBuffer(0), *hdcPrint, fxCount, fyCount2,80);
	strCount.ReleaseBuffer();
	//介质详细信息
	fyMedia = fyMediaType2 + 310;

	fxMediaCode = fxReceiveGroup2 + 720;
	fxMediaName = fxMediaCode + 200;	
	fxMediaLevel = fxMediaName + 1750;
	fxMediaNum = fxMediaLevel + 420;

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	float fyDownMediaName = fyMediaType2 + 275;
	float fyDownMediaNameEx = fyMediaType2 + 375;
	char szDownTempFileName[MAX_PATH] = {0x00};

	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		//memset(szFileName, 0x00, sizeof(szFileName));
		//strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		////去除字符串中的空格   [郝佳 2015-6-2]
		//CString strFileName;
		//strFileName.Format("%s",szDownTempFileName);
		//strFileName.TrimLeft();
		//strFileName.TrimRight();
		//strFileName.Trim();

		//memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		//strcpy(szDownTempFileName,strFileName);

		////判断最后一位是否为中文   [郝佳 2015-6-2]

		//DWORD dwText[512] = {0x00};
		//char temp[1024] = {0x00};

		//MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		//WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		//strcpy(szDownTempFileName,temp);

		//if (strlen(szDownTempFileName) > 40)
		//{
		//	CString strFileName;
		//	strFileName.Format("%s",szDownTempFileName);
		//	int nLen = 0;

		//	for (nLen=0; nLen<40; nLen++)
		//	{
		//		TCHAR szTmp1;
		//		szTmp1 = strFileName.GetAt(nLen);
		//		if (szTmp1 < 0)
		//		{
		//			if (nLen > 38)
		//			{
		//				break;
		//			}
		//			nLen++;
		//		}
		//	}

		//	strncpy(szFileName, szDownTempFileName, nLen);
		//	this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);

		//	TCHAR szExFileName[60] = {0x00};
		//	if(strlen(szDownTempFileName) > 80)
		//	{
		//		strncpy(szExFileName, szDownTempFileName + nLen, 40);
		//		this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
		//	}
		//	else
		//	{
		//		strcpy(szExFileName, szDownTempFileName + nLen);
		//		this->HDDrawText(szExFileName, *hdcPrint, fxMediaName, fyDownMediaNameEx, 80);
		//	}
		//}
		//else
		//{
		//	strcpy(szFileName, szDownTempFileName);

		//	GenLog(DEBUG_INFO, "%s[%d].文件名为：[%s]\n",__FILE__,__LINE__, szFileName);
		//	this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyDownMediaName, 80);
		//}

		this->HDDrawText(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode, *hdcPrint, fxMediaCode, fyMedia, 80);
		//this->HDDrawText(szFileName, *hdcPrint, fxMediaName, fyMedia, 80);

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		this->HDDrawText(szLevelString, *hdcPrint, fxMediaLevel, fyMedia, 80);

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		//strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		this->HDDrawText(strNum.GetBuffer(0), *hdcPrint, fxMediaNum, fyMedia, 80);
		strNum.ReleaseBuffer();

		fyMedia += 270;
		fyDownMediaName += 270;
		fyDownMediaNameEx += 270;
	}

	return 0;
}


int CHDPrinter::PrintTrackCard(PrintJob* pJobinfo, HDC hdc, RECT& rect)
{
	int nRet = 0;

	struct TAILQ_FileInfo *AdTable;
	AdTable = new TAILQ_FileInfo;
	sprintf(AdTable->filename,"%sTemplate\\00000002.emf", m_HDAppConfig->m_szRegPath);
	AdTable->offset = pJobinfo->m_JobStatusInfo.m_nStartPage;

	HENHMETAFILE hemf;
	int printerDpi_X = 600;
	int printerDpi_Y = 600;

	printerDpi_X = GetDeviceCaps(hdc, LOGPIXELSX); //获取设备X轴的DPI
	printerDpi_Y = GetDeviceCaps(hdc, LOGPIXELSY); //获取设备Y轴的DPI

	hemf = GetEnhMetaFile (AdTable->filename);
	int iExpOffset_up = GetDeviceCaps(hdc, PHYSICALOFFSETY) + (m_HDAppConfig->m_AppConfig.m_fPageUpOffset / 25.39999918) * printerDpi_Y;
	int iExpOffset_bottom = GetDeviceCaps(hdc, PHYSICALOFFSETY) + (m_HDAppConfig->m_AppConfig.m_fPageBottomOffset / 25.39999918) * printerDpi_Y;
	int iExpOffset_left = GetDeviceCaps(hdc, PHYSICALOFFSETX) + (m_HDAppConfig->m_AppConfig.m_fPageLeftOffset / 25.39999918) * printerDpi_X ;
	int iExpOffset_right = GetDeviceCaps(hdc, PHYSICALOFFSETX) + (m_HDAppConfig->m_AppConfig.m_fPageRightOffset / 25.39999918) * printerDpi_X ;

	hemf = GetEnhMetaFile (AdTable->filename);

	if(!hemf)
	{
		DeleteEnhMetaFile (hemf) ;
		hemf = NULL;
		return 0;
	}

	//可能对rect的修改导致页面放大
	rect.top   = 0 - iExpOffset_up;
	rect.bottom = ((297)/25.39999918) * printerDpi_X - iExpOffset_bottom;
	rect.left   = 0 - iExpOffset_left;
	rect.right = ((210)/25.39999918) * printerDpi_Y - iExpOffset_right;

	ENHMETAHEADER Emf_head;
	if(GetEnhMetaFileHeader(hemf,sizeof(Emf_head), (LPENHMETAHEADER)&Emf_head))
	{
		rect.top   = 0 - iExpOffset_up;		//左上角Y轴坐标
		rect.bottom = ((Emf_head.szlMillimeters.cy)/25.39999918) * printerDpi_X - iExpOffset_bottom;		//右下角Y轴坐标
		rect.left   = 0 - iExpOffset_left;		//左上角X轴坐标
		rect.right = ((Emf_head.szlMillimeters.cx)/25.39999918) * printerDpi_Y - iExpOffset_right;		//右下角X轴坐标
	}
	else
	{
		GenLog(ERROR_INFO,"%s[%d].GetEnhMetaFileHeader()失败\n",__FILE__,__LINE__);
		return 0;
	}

	if(!PlayEnhMetaFile (hdc, hemf, &rect))
	{
		GenLog(ERROR_INFO,"%s[%d].play Enhanced MetaFile Failed:%d\n",__FILE__,__LINE__,GetLastError());
		return 0;
	}
	else
	{
		nRet = rect.bottom - rect.top;

		int nXOffset = rect.left + (50 /25.39999918) * printerDpi_X;
		int nYOffset = rect.top + (86 /25.39999918) * printerDpi_Y;
		//输出图纸名称
		//this->DrawText((char*)pJobinfo->pTransprintinfo.m_event.FileTitle, hdc, nXOffset, nYOffset, 90);
		//输出图纸密级
		char szFileType[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_PrintJobInfo.nSeclvCode, szFileType);
		this->HDDrawText(szFileType, hdc, nXOffset, nYOffset, 90);
		//输出页数
		//char szFilePage[MAX_PATH] = {0x00};
		//sprintf(szFilePage, "%d 页", pJobinfo->pTransprintinfo.m_event.PageCount);
		//this->DrawText(szFilePage, hdc, nXOffset, nYOffset + 375, 90);
		//输出份数
		//char szFileCount[MAX_PATH] = {0x00};
		//sprintf(szFileCount, "%d 份", pJobinfo->pTransprintinfo.m_event.printCount);
		//this->DrawText(szFileCount, hdc, nXOffset, nYOffset + 500, 90);
	}

	return nRet;
}

//打开Word交接单模板
int CHDPrinter::OpenWordReceipt(CString strName)
{
	if(!m_pWordOffice.OpenDocument(strName))
		return -1;
	else
		return 0;
}

//编辑对应word模板书签内容
int CHDPrinter::EditWordReceipt(PrintJob *pJobinfo, TAILQ_FileInfo *fileinfo, HDC *hdc, int nNum)
{
	int iFlag = HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag;
	//打印条码
	//if(RECEIPT_DATANG == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	int nRet = 0;
	if(RECEIPT_DATANG == iFlag)
		nRet = PrintTDBMP(pJobinfo, pJobinfo->m_PrintJobInfo.nBarcodeType, nNum);//大唐条码接口
	else
		nRet = PrintBMP(pJobinfo, pJobinfo->m_PrintJobInfo.nBarcodeType, nNum);//通用条码接口

	if(-1 == nRet)
	{
		// 申请条码失败
		GenLog(ERROR_INFO, "%s[%d].PrintReceiptBarcode 失败\n", __FILE__, __LINE__);
		return -1;
	}

	CString csJobType;
	csJobType.Format("%s", pJobinfo->m_ReceiptJobInfo.szJobTypeCode);
	int nIndex = csJobType.Find(_T("CARRYOUT"));
	if (nIndex != -1)
	{
		// 外带
		EditCarryOutWordMark(pJobinfo, nNum);
	}
	else if(RECEIPT_DATANG == iFlag)
	{
		EditTDWordMark(pJobinfo, nNum); //大唐
	}
	else if(RECEIPT_7SUO == iFlag)
	{
		Edit7WordMark(pJobinfo, nNum); //7所
	}
	else if(RECEIPT_13 == iFlag)
	{
		Edit13WordMark(pJobinfo, nNum); //13所
	}
	else if(RECEIPT_CASIC == iFlag)
	{
		EditWordMark(pJobinfo, nNum); //通用
	}
	return 0;

}

//编辑通用模板书签内容
int CHDPrinter::EditWordMark(PrintJob* pJobinfo, int nNum)
{
	//上联
	//打印记录单号

	//接收单位
	m_pWordOffice.EditeBookMark(_T("接收单位上"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//报送单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s %s"), m_HDAppConfig->m_ExConfig.m_strGroupName, pJobinfo->m_ReceiptJobInfo.szUserName);
	m_pWordOffice.EditeBookMark(_T("报送单位上"), strSendGroup);

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
	m_pWordOffice.EditeBookMark(_T("介质类型上"), CString(szReciptType));

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	m_pWordOffice.EditeBookMark(_T("回执单类型上"), CString(szMediaType));

	//介质密级
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_ReceiptJobInfo.nSeclv, szSecString);
	m_pWordOffice.EditeBookMark(_T("介质密级上"), CString(szSecString));

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	m_pWordOffice.EditeBookMark(_T("数量上"), strCount);
	strCount.ReleaseBuffer();

	//介质详细信息
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szTempFileName[MAX_PATH] = {0x00};
	int iNum = 1;
	char strName[16] = {0x00};
	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体名称上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}

	//下联
	//打印记录单号

	//接收单位
	m_pWordOffice.EditeBookMark(_T("接收单位下"), pJobinfo->m_ReceiptJobInfo.szReceiveDept);

	//申请人
	m_pWordOffice.EditeBookMark(_T("申请人下"), pJobinfo->m_ReceiptJobInfo.szUserName);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	//申请时间
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(18);
	m_pWordOffice.EditeBookMark(_T("申请时间下"), strTime);
	strTime.ReleaseBuffer();

	//申请部门
	m_pWordOffice.EditeBookMark(_T("申请部门下"), pJobinfo->m_PrintJobInfo.szGroupName);

	//报送单位
	m_pWordOffice.EditeBookMark(_T("报送单位下"), m_HDAppConfig->m_ExConfig.m_strGroupName);

	//介质类型
	m_pWordOffice.EditeBookMark(_T("介质类型下"), szReciptType);

	//交接单类型
	m_pWordOffice.EditeBookMark(_T("回执单类型下"), szMediaType);

	//介质密级
	m_pWordOffice.EditeBookMark(_T("介质密级下"), szSecString);

	//显示统计	
	m_pWordOffice.EditeBookMark(_T("数量下"), strCount);
	strCount.ReleaseBuffer();
	//介质详细信息

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szDownTempFileName[MAX_PATH] = {0x00};
	iNum = 1;
	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体名称下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szDownTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}
	return 0;
}

//编辑外带交接单Word书签
int CHDPrinter::EditCarryOutWordMark(PrintJob* pJobinfo, int nNum)
{	
	//上联
	//打印记录单号

	//申请人
	m_pWordOffice.EditeBookMark(_T("申请人上"), CString(pJobinfo->m_ReceiptJobInfo.szUserName));

	//申请人部门
	m_pWordOffice.EditeBookMark(_T("申请人部门上"), CString(pJobinfo->m_PrintJobInfo.szGroupName));

	//携带人
	m_pWordOffice.EditeBookMark(_T("携带人上"), CString(pJobinfo->m_ReceiptJobInfo.szReceiver));

	//携带人部门
	m_pWordOffice.EditeBookMark(_T("携带人部门上"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//载体类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
	m_pWordOffice.EditeBookMark(_T("载体类型上"), CString(szReciptType));

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	m_pWordOffice.EditeBookMark(_T("数量上"), strCount);
	strCount.ReleaseBuffer();

	//介质详细信息
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szTempFileName[MAX_PATH] = {0x00};
	int iNum = 1;
	char strName[16] = {0x00};
	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体名称上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}

	//下联
	//打印记录单号

	//申请人
	m_pWordOffice.EditeBookMark(_T("申请人下"), CString(pJobinfo->m_ReceiptJobInfo.szUserName));

	//申请人部门
	m_pWordOffice.EditeBookMark(_T("申请人部门下"), CString(pJobinfo->m_PrintJobInfo.szGroupName));

	//携带人
	m_pWordOffice.EditeBookMark(_T("携带人下"), CString(pJobinfo->m_ReceiptJobInfo.szReceiver));

	//携带人部门
	m_pWordOffice.EditeBookMark(_T("携带人部门下"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//载体类型
	m_pWordOffice.EditeBookMark(_T("载体类型下"), CString(szReciptType));

	//显示统计	
	m_pWordOffice.EditeBookMark(_T("数量下"), strCount);
	strCount.ReleaseBuffer();
	//介质详细信息

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szDownTempFileName[MAX_PATH] = {0x00};
	iNum = 1;
	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体名称下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szDownTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}
	return 0;
}

//编辑13所交接单Word书签
int CHDPrinter::Edit13WordMark(PrintJob* pJobinfo, int nNum)
{
	//上联
	//打印记录单号

	//部门/单位
	m_pWordOffice.EditeBookMark(_T("申请单位上"), CString(pJobinfo->m_PrintJobInfo.szGroupName));

	//申请人
	m_pWordOffice.EditeBookMark(_T("申请人上"), CString(pJobinfo->m_ReceiptJobInfo.szUserName));

	//接收单位 接收人
	m_pWordOffice.EditeBookMark(_T("接收单位上"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//申请人
	m_pWordOffice.EditeBookMark(_T("接收人上"), CString(pJobinfo->m_ReceiptJobInfo.szReceiver));

	//载体类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
	m_pWordOffice.EditeBookMark(_T("介质类型上"), CString(szReciptType));

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	m_pWordOffice.EditeBookMark(_T("回执单类型上"), CString(szMediaType));

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	m_pWordOffice.EditeBookMark(_T("数量上"), strCount);
	strCount.ReleaseBuffer();

	//介质详细信息
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szTempFileName[MAX_PATH] = {0x00};
	int iNum = 1;
	char strName[16] = {0x00};
	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<3); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体名称上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}

	//下联
	//打印记录单号

	//部门/单位
	m_pWordOffice.EditeBookMark(_T("申请单位下"), CString(pJobinfo->m_PrintJobInfo.szGroupName));

	//申请人
	m_pWordOffice.EditeBookMark(_T("申请人下"), CString(pJobinfo->m_ReceiptJobInfo.szUserName));

	//接收单位 接收人
	m_pWordOffice.EditeBookMark(_T("接收单位下"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//申请人
	m_pWordOffice.EditeBookMark(_T("接收人下"), CString(pJobinfo->m_ReceiptJobInfo.szReceiver));

	//载体类型
	m_pWordOffice.EditeBookMark(_T("介质类型下"), szReciptType);

	//交接单类型
	m_pWordOffice.EditeBookMark(_T("回执单类型下"), szMediaType);

	//显示统计	
	m_pWordOffice.EditeBookMark(_T("数量下"), strCount);
	strCount.ReleaseBuffer();

	//介质详细信息

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szDownTempFileName[MAX_PATH] = {0x00};
	iNum = 1;
	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 3); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));
		strcpy(szDownTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szDownTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szDownTempFileName, 0x00, sizeof(szDownTempFileName));
		strcpy(szDownTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szDownTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szDownTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体名称下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szDownTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}

	return 0;
}

//编辑7所交接单Word书签
int CHDPrinter::Edit7WordMark(PrintJob* pJobinfo, int nNum)
{
	//上联
	//打印记录单号


	//接收单位
	m_pWordOffice.EditeBookMark(_T("接收单位上"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//报送单位
	CString strSendGroup;
	strSendGroup.Format(_T("%s %s"), m_HDAppConfig->m_ExConfig.m_strGroupName, pJobinfo->m_ReceiptJobInfo.szUserName);
	m_pWordOffice.EditeBookMark(_T("报送单位上"), strSendGroup);

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);
	m_pWordOffice.EditeBookMark(_T("介质类型上"), CString(szReciptType));

	//交接单类型
	TCHAR szMediaType[32] = {0x00};
	GetReciptTypeByCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szMediaType);
	m_pWordOffice.EditeBookMark(_T("回执单类型上"), CString(szMediaType));

	//介质密级
	char szSecString[MAX_PATH] = {0x00};
	CHDDataCenter::Instance()->GetFileTypeName(pJobinfo->m_ReceiptJobInfo.nSeclv, szSecString);
	m_pWordOffice.EditeBookMark(_T("介质密级上"), CString(szSecString));

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	m_pWordOffice.EditeBookMark(_T("数量上"), strCount);
	strCount.ReleaseBuffer();

	//介质详细信息
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szTempFileName[MAX_PATH] = {0x00};
	int iNum = 1;
	char strName[16] = {0x00};
	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<5); i++)
	{
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数上", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}

	//下联
	//打印记录单号

	//接收单位
	m_pWordOffice.EditeBookMark(_T("接收单位下"), pJobinfo->m_ReceiptJobInfo.szReceiveDept);

	//申请人
	m_pWordOffice.EditeBookMark(_T("申请人下"), pJobinfo->m_ReceiptJobInfo.szUserName);
	GenLog(DEBUG_INFO, "%s[%d].交接单中用户名：[%s]\n",__FILE__,__LINE__, pJobinfo->m_ReceiptJobInfo.szUserName);

	//申请时间
	CString strTime;
	strTime.Format(_T("%s"), pJobinfo->m_ReceiptJobInfo.szAppTime);
	strTime = strTime.Left(18);
	m_pWordOffice.EditeBookMark(_T("申请时间下"), strTime);
	strTime.ReleaseBuffer();

	//申请部门
	m_pWordOffice.EditeBookMark(_T("申请部门下"), pJobinfo->m_PrintJobInfo.szGroupName);

	//报送单位
	m_pWordOffice.EditeBookMark(_T("报送单位下"), m_HDAppConfig->m_ExConfig.m_strGroupName);

	//介质类型
	m_pWordOffice.EditeBookMark(_T("介质类型下"), szReciptType);

	//交接单类型
	m_pWordOffice.EditeBookMark(_T("回执单类型下"), szMediaType);

	//介质密级
	m_pWordOffice.EditeBookMark(_T("介质密级下"), szSecString);

	//显示统计	
	m_pWordOffice.EditeBookMark(_T("数量下"), strCount);
	strCount.ReleaseBuffer();
	//介质详细信息

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szDownTempFileName[MAX_PATH] = {0x00};
	iNum = 1;
	for (int i = nNum; (i < CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i - nNum < 5); i++)
	{
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "编号下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数下", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		strNum.ReleaseBuffer();
		iNum++;
	}
	return 0;
}

//编辑大唐交接单Word书签
int CHDPrinter::EditTDWordMark(PrintJob* pJobinfo, int nNum)
{
	//打印交接单系统时间
	CTime ct = CTime::GetCurrentTime();
	CString ctnow = ct.Format("%Y 年 %m 月 %d 日");
	m_pWordOffice.EditeBookMark(_T("打印时间"), ctnow);

	//打印记录单号


	//接收单位
	m_pWordOffice.EditeBookMark(_T("接收单位"), CString(pJobinfo->m_ReceiptJobInfo.szReceiveDept));

	//接收人员信息
	m_pWordOffice.EditeBookMark(_T("接收人"), CString(pJobinfo->m_ReceiptJobInfo.szReceiver));

	//申请部门
	CString strSendGroup;
	strSendGroup.Format(_T("%s"), m_HDAppConfig->m_ExConfig.m_strGroupName);
	m_pWordOffice.EditeBookMark(_T("申请部门"), strSendGroup);
	strSendGroup.ReleaseBuffer();

	//申请人
	CString strSendPeople;
	strSendPeople.Format(_T("%s"), pJobinfo->m_PrintJobInfo.szUserName);
	m_pWordOffice.EditeBookMark(_T("申请人"), strSendPeople);
	strSendPeople.ReleaseBuffer();

	//介质类型
	TCHAR szReciptType[32] = {0x00};
	GetReciptTypeByJobCode(pJobinfo->m_ReceiptJobInfo.szJobTypeCode,szReciptType);

	//显示统计	
	CString strCount;
	//刻录交接单
	if (strcmp(szReciptType, "光盘") == 0) 
	{			
		strCount.Format(_T("光盘数量"));
	}
	else
	{
		strCount.Format(_T("页数/份数"));
	}
	m_pWordOffice.EditeBookMark(_T("数量"), strCount);
	strCount.ReleaseBuffer();

	//介质详细信息
	char szFileName[MAX_PATH] = {0x00};

	// 第二行文件名的坐标  [7/7/2015 郝佳]
	char szTempFileName[MAX_PATH] = {0x00};
	int iNum = 1;
	char strName[16] = {0x00};
	for (int i = nNum; (i<CHDDataCenter::Instance()->m_MediaList.GetCount()) && (i-nNum<7); i++)
	{
		memset(szFileName, 0x00, sizeof(szFileName));

		strcpy(szTempFileName, CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szMediaName);

		//去除字符串中的空格   [郝佳 2015-6-2]
		CString strFileName;
		strFileName.Format("%s",szTempFileName);
		strFileName.TrimLeft();
		strFileName.TrimRight();
		strFileName.Trim();

		memset(szTempFileName, 0x00, sizeof(szTempFileName));
		strcpy(szTempFileName,strFileName);

		//判断最后一位是否为中文   [郝佳 2015-6-2]

		DWORD dwText[512] = {0x00};
		char temp[1024] = {0x00};

		MultiByteToWideChar(CP_ACP, 0, (LPCSTR)szTempFileName, -1, (LPWSTR)dwText, 512);
		WideCharToMultiByte(CP_ACP, 0, (LPWSTR)dwText, -1, temp, 1024, NULL, NULL);

		strcpy(szTempFileName,temp);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "文件名称", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szTempFileName));

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体编号", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->szBarcode));

		char szLevelString[MAX_PATH] = {0x00};
		CHDDataCenter::Instance()->GetFileTypeName(CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nSecLv, szLevelString);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "密级", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szLevelString));

		CString strNum;
		//刻录交接单
		if (strcmp(szReciptType, "光盘") == 0) 
		{			
			strNum.Format(_T("%d张"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}
		else
		{
			strNum.Format(_T("%d页/1份"), CHDDataCenter::Instance()->m_MediaList.GetAt(i)->nPageCount);
		}

		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "份数", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), strNum);
		memset(strName, 0x00, sizeof(strName));
		sprintf(strName, "%s%d", "载体类型", iNum);
		m_pWordOffice.EditeBookMark(CString(strName), CString(szReciptType));
		strNum.ReleaseBuffer();
		iNum++;
	}
	return 0;
}

//通用条码
int CHDPrinter::PrintBMP(PrintJob* pJobinfo,int barcodeType, int nFlag)
{
	// 交接单支持二维码 [4/1/2015 chenhong]
	barcodeType = 1;
	barcodeType = pJobinfo->m_ReceiptJobInfo.nBarcodeType;
	//barcodeType = m_nBarcodeType;
	pJobinfo->m_PrintJobInfo.nPosition = 1;

	char barcode[MAX_PATH] = {0x00};
	// 从服务器获取条码值 [1/8/2015 chenhong]
	if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER||m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_Batch)
	{
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=pJobinfo->m_ReceiptJobInfo.nBarcodeType;
		pApplyBarcode.nEventType = 7;//打印1 ，刻录2，交接单7
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
		if (nFlag == 0)
		{
			if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
			{
				GenLog(ERROR_INFO, "%s[%d].打印交接单申请条码失败！", __FILE__, __LINE__);
				return -1;
			}
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));	
		}
	}

	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile, ".bmp");

	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*sizeof(char));
		memcpy(barcode, pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode));
	}

	if(barcodeType == GEN39_CODE)
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}
	else if(barcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode);
	}
	else if(barcodeType == PDF417_CODE)
	{	
		// 二维码 [4/1/2015 chenhong]
		GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
		CString strBarcode;
		strBarcode.Format(_T("%s"),pJobinfo->m_szFileBarcode);
		//base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
		GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());

		PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT);
		strBarcode.ReleaseBuffer();
	}
	else
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}

	if(!FileIsExits((char *)m39bmpFile) && ((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在!\n",__FILE__,__LINE__,m39bmpFile);
		char InstallPath[MAX_PATH] = {0x00};
		sprintf_s(InstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);
		sprintf(m39bmpFile,"%s\\gougebarcode.bar",InstallPath);
	}
	const BYTE* pBits = NULL;
	const BITMAPINFO* pBMI ;

	pBMI = HDLoadBitmap(m39bmpFile);

	if(!pBMI)
		return -1;
	//float nDestHeight = pBMI->bmiHeader.biHeight / 3;
	//float nDestWidth = pBMI->bmiHeader.biWidth / 2.5;
	float nDestHeight = 0.0f;
	float nDestWidth = 0.0f;
	if((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) )
	{
		nDestWidth = pBMI->bmiHeader.biWidth * 4 / 10.0;
		nDestHeight = pBMI->bmiHeader.biHeight * 4 / 10.0;
	}
	else
	{
		nDestWidth = pBMI->bmiHeader.biWidth * 3 / 10.0;
		nDestHeight = pBMI->bmiHeader.biHeight * 3 / 10.0;
	}
	m_pWordOffice.AddPicture(CString(m39bmpFile), nDestWidth, nDestHeight);
	m_pWordOffice.EditeBookMark(_T("条码号上"), CString(barcode));
	m_pWordOffice.EditeBookMark(_T("条码号下"), CString(barcode));
	return 0;
}

//大唐条码
int CHDPrinter::PrintTDBMP(PrintJob* pJobinfo,int barcodeType, int nFlag)
{
	// 交接单支持二维码 [4/1/2015 chenhong]
	barcodeType = 1;
	//barcodeType = pJobinfo->m_ReceiptJobInfo.nBarcodeType;
	//barcodeType = m_nBarcodeType;
	pJobinfo->m_PrintJobInfo.nPosition = 1;

	char barcode[MAX_PATH] = {0x00};
	// 从服务器获取条码值 [1/8/2015 chenhong]
	if(m_HDAppConfig->m_ExConfig.m_nCreateBarcode == BARCODETYPE_SERVER)
	{
		APPLY_BARCODE pApplyBarcode;
		strcpy(pApplyBarcode.cUserID, pJobinfo->m_PrintJobInfo.szUserID);
		strcpy(pApplyBarcode.cEventCode,pJobinfo->m_PrintJobInfo.szEventCode);
		pApplyBarcode.nBarcodeType=/*pJobinfo->m_PrintJobInfo.nBarcodeType*/1;
		pApplyBarcode.nEventType = 7;//打印1 ，刻录2，交接单7
		pApplyBarcode.nCompanyType = m_HDAppConfig->m_ExConfig.m_nCompanyType;
		strcpy(pApplyBarcode.cConsoleID,m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0));
		if (nFlag == 0)
		{
			if(!CDistributeThread::Instance()->ApplyBarcode(&pApplyBarcode))
			{
				GenLog(ERROR_INFO, "%s[%d].打印交接单申请条码失败！", __FILE__, __LINE__);
				return -1;
			}
			sprintf_s(pJobinfo->m_szFileBarcode, c_nChar64, _T("%s"), m_piocp->GetBarcodeValue(GEN39_CODE));	
		}
	}

	char m39bmpFile[MAX_PATH*2] = {0x00};
	strcat(m39bmpFile, CHDDataCenter::Instance()->GetDirectory(2));
	strcat(m39bmpFile, (const char*)pJobinfo->m_PrintJobInfo.szEventCode);
	strcat(m39bmpFile, ".bmp");

	if (pJobinfo->m_bIsReceipt)
	{
		memset(barcode, 0x00, MAX_PATH*sizeof(char));
		memcpy(barcode, pJobinfo->m_szFileBarcode, strlen(pJobinfo->m_szFileBarcode));
	}

	if(barcodeType == GEN39_CODE)
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}
	else if(barcodeType == QR_CODE)
	{
		//QR 码
		QrCode qr;
		qr.AddConsoleInfo(m39bmpFile, pJobinfo->m_szFileBarcode);
	}
	else if(barcodeType == PDF417_CODE)
	{	
		// 二维码 [4/1/2015 chenhong]
		GenLog(DEBUG_INFO, "%s[%d].加密前：%s，长度：%d\n",__FILE__,__LINE__, barcode, strlen(barcode));
		CString strBarcode;
		base64_encode2((unsigned char*)barcode, strlen(barcode), strBarcode);
		GenLog(DEBUG_INFO, "%s[%d].加密后：%s，长度：%d\n",__FILE__,__LINE__, strBarcode.GetBuffer(0), strBarcode.GetLength());

		PDF417(m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, strBarcode.GetBuffer(0), PRINT);
		strBarcode.ReleaseBuffer();
	}
	else
	{
		//一维码
		Gen39Code(barcode,(char *)m39bmpFile, pJobinfo->m_PrintJobInfo.szEventCode, PRINT);
	}

	if(!FileIsExits((char *)m39bmpFile) && ((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) ||(barcodeType == PDF417_CODE)))
	{
		GenLog(ERROR_INFO,"%s[%d].条码%s 不存在!\n",__FILE__,__LINE__,m39bmpFile);
		char InstallPath[MAX_PATH] = {0x00};
		sprintf_s(InstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);
		sprintf(m39bmpFile,"%s\\gougebarcode.bar",InstallPath);
	}

	const BYTE* pBits = NULL;
	const BITMAPINFO* pBMI ;

	pBMI = HDLoadBitmap(m39bmpFile);

	if(!pBMI)
		return -1;
	//float nDestHeight = pBMI->bmiHeader.biHeight / 3;
	//float nDestWidth = pBMI->bmiHeader.biWidth / 2.5;
	float nDestHeight = 0.0f; //word条码高
	float nDestWidth = 0.0f; //word条码宽
	if((barcodeType == GEN39_CODE) ||(barcodeType == QR_CODE) )
	{
		nDestWidth = pBMI->bmiHeader.biWidth * 4 / 10.0;
		nDestHeight = pBMI->bmiHeader.biHeight * 4 / 10.0;
	}
	else
	{
		nDestWidth = pBMI->bmiHeader.biWidth * 3 / 10.0;
		nDestHeight = pBMI->bmiHeader.biHeight * 3 / 10.0;
	}
	m_pWordOffice.AddPicture(CString(m39bmpFile), nDestWidth, nDestHeight, true);
	m_pWordOffice.EditeBookMark(_T("条码号上"), CString(barcode));
	return 0;

	return 0;
}

//判断交接单类型，调用编辑交接单模板及打印输出
int CHDPrinter::PrintWordReceipt(PrintJob* pJob, LPDEVMODE devMode, HDC *hdcPrint)
{
	struct TAILQ_FileInfo *AdTable;
	AdTable = new TAILQ_FileInfo;
	char szInstallPath[MAX_PATH] = {0x00};
	//GetConsoleRegPath(szInstallPath);
	sprintf_s(szInstallPath, "%s", HDAppConfig::Instance()->m_szRegPath);

	// 打印外带单
	CString csJobType;
	csJobType.Format("%s", pJob->m_ReceiptJobInfo.szJobTypeCode);
	int nIndex = csJobType.Find(_T("CARRYOUT"));
	int nRows = 0;
	if (nIndex != -1)
	{
		nRows = 5;
		// 外带
		sprintf(AdTable->filename,"%sTemplate\\交接单WD_v5.0.dot", szInstallPath);
	}
	//判断是否为大唐定制交接单模板   [5/29/2015 haojia]
	else if (RECEIPT_13 == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag )
	{
		nRows = 3;
		sprintf(AdTable->filename,"%sTemplate\\交接单13_v5.0.dot", szInstallPath);
	}
	else if(RECEIPT_DATANG == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nRows = 7;
		sprintf(AdTable->filename,"%sTemplate\\交接单DT_v5.0.dot", szInstallPath);
	}
	else if(RECEIPT_7SUO == HDAppConfig::Instance()->m_ExConfig.m_nRecriptFlag)
	{
		nRows = 5;
		sprintf(AdTable->filename,"%sTemplate\\交接单7_v5.0.dot", szInstallPath);
	}
	else
	{
		nRows = 5;
		sprintf(AdTable->filename,"%sTemplate\\交接单_v5.0.dot", szInstallPath);
	}

	AdTable->offset = 1;

	pJob->m_JobStatusInfo.m_nStartPage = 1;	//这步主要是用于AttachBarcode函数
	pJob->m_JobStatusInfo.m_nEndPage = pJob->m_PrintJobInfo.nPageCount;		//这步主要是用于AttachBarcode函数

	devMode->dmPaperSize = 9;//默认A4纸
	devMode->dmOrientation = 1;

	// add [7/7/2014 Administrator]
	for (int i = 0; i*nRows < CHDDataCenter::Instance()->m_MediaList.GetCount(); i++)
	{
		//创建word操作对象
		HDWordOffice wordOffice;
		m_pWordOffice = wordOffice;
		m_pWordOffice.CreateApp();
		//打开对应word模板文件
		if(OpenWordReceipt(CString(AdTable->filename)) == -1)
			return -1;
		//GenLog(DEBUG_INFO, "%s[%d].打印交接单第[%d]页!\n",__FILE__,__LINE__, i + 1);
		//编辑word模板
		EditWordReceipt(pJob, AdTable, hdcPrint, i*nRows);
		//打印输出
		m_pWordOffice.PrintOut();
		//释放word
		m_pWordOffice.~HDWordOffice();
	}	

	delete AdTable;
	return 0;
}

//获取打印机句柄,调用过此函数记住ClosePrinter()
HANDLE CHDPrinter::GetPrinterHandle()
{
	PRINTER_DEFAULTS pds;
	HANDLE hPrinter = NULL;
	ZeroMemory(&pds, sizeof(PRINTER_DEFAULTS));
	pds.DesiredAccess = PRINTER_ALL_ACCESS;

	//GenLog(ERROR_INFO,"%s[%d].打开打印机：%s失败，err:%d\n",__FILE__,__LINE__, m_strPrinterPath.GetBuffer(0),GetLastError());
	if (!OpenPrinter((LPSTR)m_strPrinterPath.GetBuffer(0), &hPrinter, &pds))
	{
		GenLog(ERROR_INFO,"%s[%d].打开打印机：%s失败，err:%d\n",__FILE__,__LINE__, m_strPrinterPath.GetBuffer(0),GetLastError());
		CString str;
		str.Format("打开打印机：%s失败",m_strPrinterPath.GetBuffer(0),GetLastError());
		//MessageBox(NULL, str, _T("航盾控制台"), MB_OK);
		ShowMsgBox(str.GetBuffer(0), MB_OK);
	}
	return hPrinter;
}

//获取打印机详细信息，返回的指针用后必须以GlobalFree释放
PRINTER_INFO_2* CHDPrinter::GetInfo2()
{
	HANDLE hPrinter = GetPrinterHandle();
	PRINTER_INFO_2 *ppi2 = NULL;
	DWORD cbNeeded = 0;
	if (hPrinter)
	{
		GetPrinter(hPrinter, 2, 0, 0, &cbNeeded);
		if (cbNeeded)
		{
			ppi2 = (PRINTER_INFO_2 *)GlobalAlloc(GPTR, cbNeeded);
			if (ppi2)
			{
				if (!GetPrinter(hPrinter, 2, (LPBYTE)ppi2, cbNeeded, &cbNeeded))
				{
					GlobalFree((HGLOBAL)ppi2);
					ppi2 = NULL;
				}
			}
		}
		ClosePrinter(hPrinter);
	}
	
	return ppi2;
}
//输出打印机参数
BOOL CHDPrinter::GetPInfo2(PRINTER_INFO_2 *ppi2)
{
	HANDLE hPrinter = GetPrinterHandle();
	BOOL bOk = FALSE;
	DWORD fMode;

	//dagnwei 20140816
	DWORD dwNeeded,dwRet;
	LPDEVMODE pDevModeW = NULL;
	dwNeeded = DocumentProperties(NULL, hPrinter,
		ppi2->pPrinterName,
		NULL,
		NULL,
		0);

	if(dwNeeded > 0)
	{
		pDevModeW = (LPDEVMODE)malloc(dwNeeded);

	}
	dwRet = DocumentProperties(NULL, hPrinter,
		ppi2->pPrinterName,
		pDevModeW, //The address of the buffer to fill
		NULL,
		DM_OUT_BUFFER);
	if(dwRet != IDOK)
	{
		free(pDevModeW);
		pDevModeW = NULL;
	}
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmSpecVersion：%d\n", __FILE__, __LINE__, pDevModeW->dmSpecVersion);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDriverVersion：%d\n", __FILE__, __LINE__, pDevModeW->dmDriverVersion);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmSize：%d\n", __FILE__, __LINE__, pDevModeW->dmSize);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDriverExtra：%d\n", __FILE__, __LINE__, pDevModeW->dmDriverExtra);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmFields：%d\n", __FILE__, __LINE__, pDevModeW->dmFields);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmOrientation：%d\n", __FILE__, __LINE__, pDevModeW->dmOrientation);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperSize：%d\n", __FILE__, __LINE__, pDevModeW->dmPaperSize);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperLength：%d\n", __FILE__, __LINE__, pDevModeW->dmPaperLength);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPaperWidth：%d\n", __FILE__, __LINE__, pDevModeW->dmPaperWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmScale：%d\n", __FILE__, __LINE__, pDevModeW->dmScale);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmCopies：%d\n", __FILE__, __LINE__, pDevModeW->dmCopies);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDefaultSource：%d\n", __FILE__, __LINE__, pDevModeW->dmDefaultSource);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPrintQuality：%d\n", __FILE__, __LINE__, pDevModeW->dmPrintQuality);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPosition：%d\n", __FILE__, __LINE__, pDevModeW->dmPosition.x);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPosition：%d\n", __FILE__, __LINE__, pDevModeW->dmPosition.y);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayOrientation：%d\n", __FILE__, __LINE__, pDevModeW->dmDisplayOrientation);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayFixedOutput：%d\n", __FILE__, __LINE__, pDevModeW->dmDisplayFixedOutput);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmColor：%d\n", __FILE__, __LINE__, pDevModeW->dmColor);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDuplex：%d\n", __FILE__, __LINE__, pDevModeW->dmDuplex);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmYResolution：%d\n", __FILE__, __LINE__, pDevModeW->dmYResolution);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmTTOption：%d\n", __FILE__, __LINE__, pDevModeW->dmTTOption);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmCollate：%d\n", __FILE__, __LINE__, pDevModeW->dmCollate);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmFormName：%s\n", __FILE__, __LINE__, pDevModeW->dmFormName);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmLogPixels：%d\n", __FILE__, __LINE__, pDevModeW->dmLogPixels);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmBitsPerPel：%d\n", __FILE__, __LINE__, pDevModeW->dmBitsPerPel);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPelsWidth：%d\n", __FILE__, __LINE__, pDevModeW->dmPelsWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPelsHeight：%d\n", __FILE__, __LINE__, pDevModeW->dmPelsHeight);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayFlags：%d\n", __FILE__, __LINE__, pDevModeW->dmDisplayFlags);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmNup：%d\n", __FILE__, __LINE__, pDevModeW->dmNup);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDisplayFrequency：%d\n", __FILE__, __LINE__, pDevModeW->dmDisplayFrequency);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmICMMethod：%d\n", __FILE__, __LINE__, pDevModeW->dmICMMethod);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmMediaType：%d\n", __FILE__, __LINE__, pDevModeW->dmMediaType);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmDitherType：%d\n", __FILE__, __LINE__, pDevModeW->dmDitherType);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmReserved1：%d\n", __FILE__, __LINE__, pDevModeW->dmReserved1);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmReserved2：%d\n", __FILE__, __LINE__, pDevModeW->dmReserved2);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPanningWidth：%d\n", __FILE__, __LINE__, pDevModeW->dmPanningWidth);
	GenLog(DEBUG_INFO, "%s[%d].输出前devMode->dmPanningHeight：%d\n", __FILE__, __LINE__, pDevModeW->dmPanningHeight);
	if(pDevModeW)
	{
		free(pDevModeW);
		pDevModeW=NULL;
	}
	return 0;
}
//打印机设置
//************************************
// Method:    SetInfo2
// FullName:  CHDPrinter::SetInfo2
// Access:    private 
// Returns:   BOOL
// Qualifier:
// Parameter: PRINTER_INFO_2 * ppi2
// 针对任务中每页进行打印机参数设置，仅在页与页间纸张、横纵向发生变化时进行调用。
//发生过setprinterparam函数设置参数，本函数紧接着又进行参数设置导致A3打成A4的故障现象。
//日期： 2014-08-16
//修改人：党伟 石春刚。
//************************************
BOOL CHDPrinter::SetInfo2(PRINTER_INFO_2 *ppi2)
{

	GenLog(ERROR_INFO, "%s[%d].SetInfo2\n",__FILE__,__LINE__);
	HANDLE hPrinter = GetPrinterHandle();
	BOOL bOk = FALSE;
	DWORD fMode;

	GenLog(ERROR_INFO, "%s[%d].ppi2->pPrinterName:%s\n",__FILE__,__LINE__,ppi2->pPrinterName);
	//dagnwei 20140816
	DWORD dwNeeded,dwRet;
	LPDEVMODE pDevModeW = NULL;
	int iNeeded = DocumentProperties(NULL, hPrinter,
		ppi2->pPrinterName,
		NULL,
		NULL,
		0);

	GenLog(ERROR_INFO, "%s[%d].dwNeeded>0:%d\n",__FILE__,__LINE__, iNeeded);

	GenLog(ERROR_INFO, "%s[%d].dwNeeded>0:%d\n",__FILE__,__LINE__, GetLastError());
	if(iNeeded > 0)
	{
		GenLog(ERROR_INFO, "%s[%d].dwNeeded>0:%d\n",__FILE__,__LINE__, GetLastError());
		pDevModeW = (LPDEVMODE)malloc(iNeeded);
	GenLog(ERROR_INFO, "%s[%d].dwNeeded>0:%d\n",__FILE__,__LINE__, GetLastError());

	}
	else
	{
		GenLog(ERROR_INFO, "%s[%d].dwNeeded>0:%d\n",__FILE__,__LINE__, iNeeded);
		//pDevModeW = (LPDEVMODE)malloc(iNeeded);
		GenLog(ERROR_INFO, "%s[%d].dwNeeded>0:%d\n",__FILE__,__LINE__, GetLastError());
	}
	if (!pDevModeW) 
	{
		GenLog(ERROR_INFO, "%s[%d].dwNeeded<0:%d:%d\n",__FILE__,__LINE__,structdevmodeSize, GetLastError());
		//重新获取打印机缓存
	pDevModeW = (LPDEVMODE)malloc(structdevmodeSize);

		
	}
	GenLog(ERROR_INFO, "%s[%d].dwNeeded<0:%d:%d\n",__FILE__,__LINE__,structdevmodeSize, GetLastError());

	dwRet = DocumentProperties(NULL, hPrinter,
		ppi2->pPrinterName,
		pDevModeW, //The address of the buffer to fill
		NULL,
		DM_OUT_BUFFER);
		
	GenLog(ERROR_INFO, "%s[%d].dwRet%ld\n",__FILE__,__LINE__,dwRet);
	if(dwRet != IDOK)
	{

		GenLog(ERROR_INFO, "%s[%d].dwRet%ld\n",__FILE__,__LINE__,dwRet);
		if(pDevModeW!=NULL)
		{
		free(pDevModeW);
		pDevModeW = NULL;
		}
	}

	GenLog(ERROR_INFO, "%s[%d].dwRet%ld\n",__FILE__,__LINE__,dwRet);
	pDevModeW->dmPaperSize = ppi2->pDevMode->dmPaperSize;

	GenLog(ERROR_INFO, "%s[%d].	pDevModeW->dmPaperSize%d\n",__FILE__,__LINE__,	pDevModeW->dmPaperSize);
	GenLog(ERROR_INFO, "%s[%d].DocumentProperties\n",__FILE__,__LINE__);
	if (hPrinter)
	{
		fMode = DM_IN_BUFFER | DM_OUT_BUFFER;//dangweixiugai
		bOk = (DocumentProperties(NULL, hPrinter,
			ppi2->pPrinterName,
			ppi2->pDevMode,//dangweixiugai
			pDevModeW,//ppi2->pDevMode,//dangweixiugai
			fMode) == IDOK);//&&
		//::SetPrinter(hPrinter, 2, (LPBYTE)ppi2, 0));

		if(pDevModeW->dmPaperSize != ppi2->pDevMode->dmPaperSize)
		{
			DocumentProperties(NULL, hPrinter,
				ppi2->pPrinterName,
				ppi2->pDevMode,//pDevModeW,//ppi2->pDevMode,//dangweixiugai
				pDevModeW,//ppi2->pDevMode,//dangweixiugai
				fMode);

			GenLog(DEBUG_INFO, "%s[%d].针对每页进行设置页面大小 \n",__FILE__,__LINE__);
		}


		ClosePrinter(hPrinter);
	}
	

	GenLog(ERROR_INFO, "%s[%d].DocumentProperties\n",__FILE__,__LINE__);
	if(pDevModeW)
	{
		free(pDevModeW);
	}
	return bOk;
}

// 第二种打印方法 [3/27/2014 wangchao]
BOOL CHDPrinter::PrintOneDoc2(PrintJob* pJobinfo, int index)
{
	//注释里要体现文件编号和第几份
	BOOL bStatus = FALSE;
	int i = 0;
	int iJobid = 0;

#ifdef CETC_PDF417_CODE
	GenerateCETCBarcode(pJobinfo);
#else
	// 网络模式 [10/15/2014 chenhong]
	if (CHDDataCenter::Instance()->GetWorkingModel() == WORKING_NETWORK || strcmp(m_HDAppConfig->m_ExConfig.m_strGroupCode.GetBuffer(0),"CAEP") == 0)
	{
		if (GenerateCAEPBarcode(pJobinfo) == 0)
		{
			GenLog(ERROR_INFO, "%s[%d].打印任务%s申请大流水号失败！", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
			return -1;
		}
	}
	else
	{
		if (GenerateBarcode(pJobinfo) == 0)
		{
			GenLog(ERROR_INFO, "%s[%d].打印任务%s申请大流水号失败！", __FILE__, __LINE__, pJobinfo->m_PrintJobInfo.szFileName);
			return -1;
		}
	}
#endif

	char szDocNameBuf[MAX_PATH*2] = {0x00};

	HANDLE hPrinter = NULL;
	DOC_INFO_1 DocInfo;
	DWORD      dwJob = 0L;
	DWORD      dwBytesWritten = 0L;

	sprintf(szDocNameBuf,"HDPrint:Console-%s#Document-%s$", m_HDAppConfig->m_AppConfig.m_strConsoleID.GetBuffer(0), pJobinfo->m_szFileBarcode);

	bStatus = OpenPrinter((LPSTR)m_strPrinterPath.GetBuffer(0), &hPrinter, NULL );
	if (bStatus)
	{
		// Fill in the structure with info about this "document." 
		DocInfo.pDocName = szDocNameBuf;
		DocInfo.pOutputFile = NULL;
		DocInfo.pDatatype = "NT EMF 1.008";

		struct TAILQ_FileInfo *p,*next;
		if (!TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList))
		{
			GenLog(ERROR_INFO,"%s[%d].TAILQ_FIRST错误，发送解锁包！\n",__FILE__, __LINE__);
			bStatus = FALSE;
		}
		else
		{
			// Inform the spooler the document is beginning. 
			dwJob = StartDocPrinter( hPrinter, 1, (LPBYTE)&DocInfo);
			DWORD dwRet = ::GetLastError();
			if (dwJob > 0)
			{
				//int usageTmp = atoi((const char*)pJobinfo->m_PrintJobInfo.m_szComment);
				for (p = TAILQ_FIRST(&pJobinfo->m_JobStatusInfo.m_uPageList.PageList); p; p = next)
				{
					next = TAILQ_NEXT(p, chain);

					/*****************************高级打印中选择补打的起始页和结束页****************************/
					if (pJobinfo->m_JobStatusInfo.m_nStartPage != 0)
					{
						if (pJobinfo->m_JobStatusInfo.m_nStartPage > pJobinfo->m_PrintJobInfo.nPageCount)
						{
							//起始页超过了总页数，报错
							return -1;
						}
						else
						{
							if(pJobinfo->m_JobStatusInfo.m_nStartPage > p->offset)
								continue;
						}
					}
					else
						pJobinfo->m_JobStatusInfo.m_nStartPage = 1;	//这步主要是用于AttachBarcode函数

					if (pJobinfo->m_JobStatusInfo.m_nEndPage != 0)
					{
						if (pJobinfo->m_JobStatusInfo.m_nEndPage > pJobinfo->m_PrintJobInfo.nPageCount)
						{
							//结束页大于页数了，也不对
							return -1;
						}
						else
						{
							if(pJobinfo->m_JobStatusInfo.m_nEndPage < p->offset)
								break;
						}
					}
					else
						pJobinfo->m_JobStatusInfo.m_nEndPage = pJobinfo->m_PrintJobInfo.nPageCount;		//这步主要是用于AttachBarcode函数


					int papersize = 0;
					short paperorientation;
					//先取得纸张横纵向
					papersize = GetPaperSize(pJobinfo->m_PrintJobInfo.nPrintType,p, &paperorientation);

					PRINTER_INFO_2 *ppi2 = GetInfo2();
					if (ppi2)
					{
						//papersize = this->GetPaperSize(m_strPrinterPath, ppi2->pPortName, (TCHAR*)pJobinfo->m_PrintJobInfo.m_szPageSize);
						if (papersize > 0)
						{
							ppi2->pDevMode->dmFields = DM_PAPERSIZE|DM_PAPERWIDTH|DM_PAPERLENGTH|DM_ORIENTATION;
							ppi2->pDevMode->dmPaperSize = papersize;
							ppi2->pDevMode->dmPaperWidth = 0;
							ppi2->pDevMode->dmPaperLength = 0;
							ppi2->pDevMode->dmOrientation = paperorientation;
							bStatus = SetInfo2(ppi2);
						}
						GlobalFree((HGLOBAL)ppi2);

						// Start a page. 
						bStatus = StartPagePrinter( hPrinter );
						if (bStatus)
						{
							HENHMETAFILE hemf = GetEnhMetaFile (p->filename);
							UINT cbBuffer = GetEnhMetaFileBits(hemf, 0, NULL);

							if (cbBuffer > 0)
							{
								BYTE* pBuffer = (BYTE*)malloc(cbBuffer*sizeof(BYTE));
								GetEnhMetaFileBits(hemf, cbBuffer, pBuffer);

								// Send the data to the printer. 
								DWORD dwBytesWritten = 0;
								bStatus = WritePrinter( hPrinter, pBuffer, cbBuffer, &dwBytesWritten);
								EndPagePrinter (hPrinter);
								DeleteEnhMetaFile(hemf);
								free(pBuffer);
							}
						}
					}
				}

				// Inform the spooler that the document is ending. 
				EndDocPrinter( hPrinter );
			}
			else
			{
				bStatus = FALSE; 
			}
			// Close the printer handle. 
			ClosePrinter( hPrinter );
		}
	}

	return bStatus;
}

//增加规格自定义纸张
//szPaperName: 自定义纸张名称
//PaperSize: 纸张的大小，以0.1mm为单位
//rcPrintableMargin: 打印机的最小可打印边界，以0.1mm为单位。
// 可参见GetDeviceCaps函数说明中的PHYSICALOFFSETX及PHYSICALOFFSETY
BOOL CHDPrinter::AddCustomPaper(PAPERNAME szPaperName, SIZE PaperSize, RECT rcPrintableMargin)
{
	BOOL bOk = FALSE;
	if (IsWindowsNT()) //Windows NT4/2000/XP才支持
	{
		FORM_INFO_1 fi1;
		fi1.Flags = FORM_USER;
		fi1.pName = (LPTSTR)szPaperName;
		fi1.Size.cx = PaperSize.cx * 100;
		fi1.Size.cy = PaperSize.cy * 100;
		fi1.ImageableArea.left = rcPrintableMargin.left * 100;
		fi1.ImageableArea.top = rcPrintableMargin.top * 100;
		fi1.ImageableArea.right = fi1.Size.cx - rcPrintableMargin.right * 100;
		fi1.ImageableArea.bottom = fi1.Size.cy - rcPrintableMargin.bottom * 100;
		HANDLE hPrinter = GetPrinterHandle();
		if (hPrinter)
		{
			bOk = (SetForm(hPrinter, (LPSTR)szPaperName, 1, (LPBYTE)&fi1) || //已存在该类型纸张则更改
				AddForm(hPrinter, 1, (LPBYTE)&fi1)); //否则添加此自定义纸张
			ClosePrinter(hPrinter);
		}
	}
	return bOk;
}

//删除自定义规格纸张
BOOL CHDPrinter::DeleteCustomPaper(LPCTSTR szPaperName)
{
	BOOL bOk = FALSE;
	if (IsWindowsNT()) //Windows NT4/2000/XP才支持
	{
		HANDLE hPrinter = GetPrinterHandle();
		if (hPrinter)
		{
			bOk = DeleteForm(hPrinter, (LPSTR)szPaperName);
			ClosePrinter(hPrinter);
		}
	}
	return bOk;
}

//设置打印机的默认纸张和方向
BOOL CHDPrinter::SetPaper(PAPERNAME szPaperName, short nOrientation)
{
	BOOL bOk = FALSE;
	PRINTER_INFO_2 *ppi2 = GetInfo2();
	if (ppi2)
	{
		short nPaperSize = GetPaperSize(ppi2->pPortName, szPaperName);
		if (nPaperSize != -1)
		{
			ppi2->pDevMode->dmFields = DM_PAPERSIZE|DM_PAPERWIDTH|DM_PAPERLENGTH|DM_ORIENTATION;
			ppi2->pDevMode->dmPaperSize = nPaperSize;
			ppi2->pDevMode->dmPaperWidth = 0;
			ppi2->pDevMode->dmPaperLength = 0;
			ppi2->pDevMode->dmOrientation = nOrientation;
			bOk = SetInfo2(ppi2);
		}
		GlobalFree((HGLOBAL)ppi2);
	}
	return bOk;
}

//由纸张名称得到对应的DEVMODE中的那个dmPaperSize值，返回0表示有错误
short CHDPrinter::GetPaperSize(LPCTSTR szPortName, PAPERNAME szPaperName)
{
	short nPaperSize = 0;
	//获得可用打印机纸张类型数目
	int nNeeded = DeviceCapabilities(m_strPrinterPath.GetBuffer(0), szPortName, DC_PAPERNAMES, NULL, NULL);
	if (nNeeded)
	{
		PAPERNAME *pszPaperNames = new PAPERNAME[nNeeded]; //分配纸张名称数组
		//获得可用打印机纸张名称数组
		if (DeviceCapabilities(m_strPrinterPath.GetBuffer(0), szPortName, DC_PAPERNAMES, (LPTSTR)pszPaperNames, NULL) != -1)
		{
			int i;
			//查找纸张类型szPaperName在数组中的索引
			for (i = 0; i < nNeeded && _tcscmp(pszPaperNames[i], szPaperName); i++);
			if (i < nNeeded)
			{
				//获得可用打印机纸张尺寸号数目(应该等于打印机纸张类型数目)
				nNeeded = DeviceCapabilities(m_strPrinterPath.GetBuffer(0), szPortName, DC_PAPERS, NULL, NULL);
				if (nNeeded)
				{
					LPWORD pPapers = new WORD[nNeeded]; //分配纸张尺寸号数组
					//获得可用打印机纸张尺寸号数组
					if (DeviceCapabilities(m_strPrinterPath.GetBuffer(0), szPortName, DC_PAPERS, (LPSTR)pPapers, NULL) != -1)
						nPaperSize = pPapers[i]; //获得纸张类型szPaperName对应的尺寸号
					delete []pPapers;
				}
			}
		}
		delete []pszPaperNames;
	}
	return nPaperSize;
}

//FALSE是不可以，TRUE是可以
BOOL CHDPrinter::IsSendNext()
{
	if (m_listSendedInfo.GetCount() >= HDAppConfig::Instance()->m_ExConfig.m_nMaxJob)//3应该是可配的
	{
		return FALSE;
	}
	else
	{
		return TRUE;
	}

	return FALSE;
}

//Clear m_listSendedJob
void CHDPrinter::ClearSendedJob()
{
	for (int i = 0; i < m_listSendedJob.GetCount(); i++)
	{
		delete m_listSendedJob.GetAt(i);
	}
	m_listSendedJob.RemoveAll();
	m_listSendedJob.SetSize(0);
}


//Clear m_listJob
void CHDPrinter::ClearUnSendedJob()
{
	for (int i = 0; i < m_listJob.GetCount(); i++)
	{
		delete m_listJob.GetAt(i);
	}
	m_listJob.RemoveAll();
	m_listJob.SetSize(0);
}



//根据任务号获取任务信息
PrintJob* CHDPrinter::GetSendedJob(TCHAR* szEventCode)
{
	for (int i = 0; i < m_listSendedJob.GetCount(); i++)
	{
		if (strcmp(szEventCode, m_listSendedJob.GetAt(i)->m_PrintJobInfo.szEventCode) == 0)
		{
			return m_listSendedJob.GetAt(i);
		}
	}

	return NULL;
}

//根据任务号获取任务信息
BOOL CHDPrinter::GetPrinterSendedJob(TCHAR* szEventCode)
{
	for (int i = 0; i < m_listSendedInfo.GetCount(); i++)
	{
		if (strcmp(szEventCode, m_listSendedInfo.GetAt(i)->szPrintJobID) == 0)
		{
			return TRUE;
		}
	}

	return FALSE;
}

//获取打印机状态
DWORD CHDPrinter::GetPrinterStatus()
{
	DWORD dwRet =  0;
	PRINTER_INFO_2 *ppi2 = GetInfo2();
	if(ppi2==NULL)
	{
		return 2; 
	}
	dwRet = ppi2->Status;
	GlobalFree(ppi2);

	return dwRet;
}

BOOL CHDPrinter::PDFTOBMP(char *filepathPDFfull,char *filepathPDF,BOOL bIsTemp)
{
	BOOL bresult = FALSE;
	DWORD dwExitCode;
	char tem[1000] = {0x00};
	char olddir[260] = {0x00};
	char newdir[260] ={0x00};
	char exedir[260] ={0x00};

	char cdir[MAX_PATH]={0x00};
	char cfilename[MAX_PATH]={0x00};
	char cext[32]={0x00};
	char cDirTem[MAX_PATH] = {0x00};
	char cdriv[32]={0x00};
	//********************************Pipe
	SECURITY_ATTRIBUTES sa;  
	HANDLE hRead,hWrite;  
	sa.nLength = sizeof(SECURITY_ATTRIBUTES);  
	sa.lpSecurityDescriptor = NULL;  
	sa.bInheritHandle = TRUE;  
	_splitpath(filepathPDFfull,NULL,NULL,cfilename,NULL);
	GenLog(DEBUG_INFO,"%s[%d]进入转换pdf流程...%s\n",__FILE__,__LINE__,cfilename);
	if (!CreatePipe(&hRead,&hWrite,&sa,30000))   
	{  
		CloseHandle(hWrite);  
		CloseHandle(hRead);  
		GenLog(DEBUG_INFO,"%s[%d].PDF to BMP管道创建失败！\n",__FILE__,__LINE__);
		return FALSE;  
	}  

	STARTUPINFO si ;
	si.cb = sizeof(STARTUPINFO);
	GetStartupInfo(&si); 
	PROCESS_INFORMATION pi;
	si.hStdError = GetStdHandle(STD_ERROR_HANDLE);
	si.hStdInput = hRead;
	si.hStdOutput = hWrite;    
	si.dwFlags = STARTF_USESHOWWINDOW | STARTF_USESTDHANDLES;
	si.wShowWindow = SW_HIDE;

	sprintf(tem,"\"%sAcrobat.pdf2image.exe\" \"%s\" \"%s\"",  m_HDAppConfig->m_szRegPath,filepathPDFfull,filepathPDF);
	GenLog(DEBUG_INFO,"%s[%d].调用Acrobat.pdf2image.exe路径为 %s ！\n", __FILE__, __LINE__, tem);

	BOOL bRet = ::CreateProcess(NULL,tem,NULL,NULL,TRUE,CREATE_NEW_CONSOLE,NULL,NULL,&si,&pi);
	DWORD aeer = GetLastError();

	if(bRet)
	{
		//char buffer[8182] = {0};           
		//DWORD bytesRead; 
		//CString strBuffer;
		//int ncountbegin = GetTickCount();
		//int ncountend=0;
		//DWORD in_buf_size =0;
		//while (1)   
		//{  		
		WaitForSingleObject(pi.hProcess, INFINITE);		// 等待子进程的退出	
		GetExitCodeProcess(&pi.hProcess, &dwExitCode);	// 获取子进程的退出码
		//	PeekNamedPipe(hRead,NULL, 0, NULL, &in_buf_size, NULL);
		//	if(in_buf_size!=0)
		//	{
		//		ncountbegin = GetTickCount();
		//		if (ReadFile(hRead,buffer,4095,&bytesRead,NULL))  
		//		{
		//			strBuffer += buffer;
		//			if(strBuffer.Find(_T("LastPage"))!=-1)
		//			{
		//				//mydebug(DEBUG_INFO,"%s[%d]LastPage %s\n",__FILE__,__LINE__,cfilename);
		//				GenLog(DEBUG_INFO,"%s[%d]LastPage %s\n",__FILE__,__LINE__,cfilename);
		//				bresult = TRUE;
		//				break;
		//			}
		//			else if(strBuffer.Find(_T("Error"))!=-1)
		//			{
		//				//mydebug(ERROR_INFO,"%s[%d]trans error %s\n",__FILE__,__LINE__,cfilename);
		//				GenLog(DEBUG_INFO,"%s[%d]trans error %s\n",__FILE__,__LINE__,cfilename);
		//				bresult = FALSE;
		//				break;
		//			}
		//		}
		//		else
		//		{
		//			//mydebug(ERROR_INFO,"%s[%d]read error %s\n",__FILE__,__LINE__,cfilename);
		//			GenLog(DEBUG_INFO,"%s[%d]read error %s\n",__FILE__,__LINE__,cfilename);
		//			bresult = FALSE;
		//			break;
		//		}
		//	}
		//	else
		//	{
		//		//mydebug(ERROR_INFO,"%s[%d]GS dwExitCode = %d  %s \n",__FILE__,__LINE__,dwExitCode,cfilename);
		//		GenLog(DEBUG_INFO,"%s[%d]GS dwExitCode = %d  %s \n",__FILE__,__LINE__,dwExitCode,cfilename);
		//		bresult = FALSE;
		//		break;
		//	}
		//} 	
		CloseHandle(hWrite);  
		CloseHandle(hRead);  
		CloseHandle(pi.hThread);		
		CloseHandle(pi.hProcess); 
		bresult = TRUE;
		GenLog(DEBUG_INFO,"%s[%d]pdf转BMP成功。%s\n",__FILE__,__LINE__);
	}
	else
	{
		CloseHandle(hWrite);  
		CloseHandle(hRead);  
		bresult = FALSE;
		GenLog(DEBUG_INFO,"%s[%d]pdf流程失败。%s\n",__FILE__,__LINE__);
	}
	if(bIsTemp)
	{
		DeleteFile(filepathPDFfull);
	}
	//mydebug(DEBUG_INFO,"%s[%d]退出转换pdf流程。%s\n",__FILE__,__LINE__,cfilename);
	GenLog(DEBUG_INFO,"%s[%d]退出转换pdf流程。%s\n",__FILE__,__LINE__,cfilename);
	return bresult;
}
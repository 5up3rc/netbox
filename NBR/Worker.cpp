// Worker.cpp : implementation file
//

#include "stdafx.h"
#include <atlpath.h>
#include "NBR.h"
#include "Worker.h"
#include "LogDialog.h"

#include "ZipFile.h"

#define MK_9465 0x94659465

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// CWorker

CWorker::CWorker()
{
	m_bAutoDelete = FALSE;
}

BOOL CWorker::LogLastError(LPCTSTR pstrFile)
{
	LPTSTR lpMsgBuf;
	if (FormatMessage(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
			NULL, GetLastError(), MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT), (LPTSTR) &lpMsgBuf, 0, NULL ))
	{
		m_pwndLog->WriteLog(_T("\r\n") + GetMessage(L"file") + _T(": "));
		m_pwndLog->WriteLog(pstrFile);

		m_pwndLog->WriteLog(_T("\r\n") + GetMessage(L"error") + _T(": "));
		m_pwndLog->WriteLog(lpMsgBuf);
		LocalFree(lpMsgBuf);
	}

	return FALSE;
}

BOOL CWorker::StartBuild()
{
	BYTE buffer[2048];
	UINT nSize;
	MD5_CTX m_ctxMD5;
	CString str;
	CPath path;

	m_strRootPath = m_pwndMain->m_strSrcFolder;
	m_pwndMain->m_wndOutput.GetWindowText(str);
	path.m_strPath = m_strRootPath;
	path.Append(str);
	str = path.m_strPath;
	m_pwndLog->WriteLog(GetMessage(L"outFile") + _T(": \""));
	m_pwndLog->WriteLog(str);
	if(!m_pwndLog->WriteLog(_T("\" ...\r\n\r\n")))
		return FALSE;

	if(!m_file.Open(str, CFile::shareDenyRead | CFile::modeReadWrite | CFile::typeBinary | CFile::modeCreate))
		return LogLastError(str);

	if(m_pwndMain->IsDlgButtonChecked(IDC_LIBRARY))
	{
		RAND_bytes(buffer, sizeof(buffer));
		m_file.Write(buffer, (*(WORD*)(buffer + sizeof(buffer) - 2)) % sizeof(buffer));
	}else
	{
		CFile fileNetBox;

		str = theApp.m_strBasePath + _T("NetBox.exe");

		if(!fileNetBox.Open(str, CFile::shareDenyNone | CFile::modeRead | CFile::typeBinary))
			return LogLastError(str);

		while(nSize = fileNetBox.Read(buffer, sizeof(buffer)))
			m_file.Write(buffer, nSize);

//		m_file.Seek(0x7c, 0);
//		m_file.Write(&theApp.m_certinfo.m_nDevID, 4);
	}
	m_file.SeekToBegin();

	ZeroMemory(m_dwPackInfo, sizeof(m_dwPackInfo));

	MD5_Init(&m_ctxMD5);
	do
	{
		nSize = m_file.Read(buffer, sizeof(buffer));
		MD5_Update(&m_ctxMD5, buffer, nSize);
		m_dwPackInfo[0] += nSize;
	}while(nSize == sizeof(buffer));

	MD5_Final(m_MD5Pass, &m_ctxMD5);

	m_dwPackInfo[1] = 0;
	m_dwPackInfo[2] = MK_9465;

	RAND_bytes(buffer, MD5_DIGEST_LENGTH);
	m_file.Write(buffer, MD5_DIGEST_LENGTH);

	for(int i = 0; i < MD5_DIGEST_LENGTH; i ++)
		m_MD5Pass[i] ^= buffer[i];

	m_IndexFile.Write(buffer, 512);

/*	CFile file;

	str = theApp.m_strBasePath + _T("nbdw.lic");
	if(file.Open(str, CFile::modeRead | CFile::shareDenyNone))
	{
		char buffer[512];

		file.Read(buffer, 512);

		file.Close();

		m_IndexFile.Write(buffer, 512);
	}else return LogLastError(str);*/

	return TRUE;
}

BOOL CWorker::PackOneFile(CString strFileName)
{
	CString str;
	CStringW strFile(strFileName);

	DWORD dwPos = (DWORD)m_file.GetPosition();

	if(strFile[strFile.GetLength() - 1] == _T('\\'))
		m_strLogString += GetMessage(L"Folder") + _T(": ");
	else m_strLogString += GetMessage(L"file") + _T(": ");
	m_strLogString += strFile;
	m_strLogString += _T("\r\n");
	m_logCount ++;

	strFile = m_strRootPath + strFile;

	if(m_logCount > 3)
	{
		if(!m_pwndLog->WriteLog(m_strLogString))return FALSE;
		m_logCount = 0;
		m_strLogString.Empty();
	}

	if(strFile[strFile.GetLength() - 1] == _T('\\'))
	{
		strFile.ReleaseBuffer(strFile.GetLength() - 1);
		m_IndexFile.Write((LPCWSTR)strFile + m_strRootPath.GetLength() - 1,
					(strFile.GetLength() + 2 - m_strRootPath.GetLength()) * sizeof(WCHAR));

		dwPos = 0xFFFFFFFF;
		m_IndexFile.Write(&dwPos, sizeof(DWORD));
	}else
	{
		m_IndexFile.Write((LPCWSTR)strFile + m_strRootPath.GetLength() - 1,
					(strFile.GetLength() + 2 - m_strRootPath.GetLength()) * sizeof(WCHAR));
		m_IndexFile.Write(&dwPos, sizeof(DWORD));

		CZipFile zipFile;
		CFile f;

		zipFile.SetKey(m_MD5Pass);

		zipFile.Open(m_file, CFile::modeWrite);
		str = strFile;
		if(f.Open(str, CFile::shareDenyNone | CFile::modeRead | CFile::typeBinary))
		{
			CFileStatus fs;
			CBDate d;

			f.GetStatus(fs);
			d = fs.m_mtime.GetTime();
			m_IndexFile.Write(&fs.m_size, sizeof(fs.m_size));
			m_IndexFile.Write(&d, sizeof(d));

			if(f.GetLength() > 4096)
			{
				if(!m_pwndLog->WriteLog(m_strLogString))return FALSE;
				m_logCount = 0;
				m_strLogString.Empty();
			}

			static char buf[10240];
			long n;

			while(n = f.Read(buf, sizeof(buf)))
			{
				zipFile.Write(buf, n);
				if(n = sizeof(buf) && !m_pwndLog->WriteLog(""))return FALSE;
			}
		}else return LogLastError(str);
	}

	return TRUE;
}

BOOL CWorker::PackFile()
{
	int n = m_pwndMain->m_wndSource.GetItemCount();
	CString strStartup;

	m_logCount = 0;

	if(m_pwndMain->IsDlgButtonChecked(IDC_APPLICATION))
	{
		m_pwndMain->m_wndStartup.GetWindowText(strStartup);
		if(!PackOneFile(strStartup))return FALSE;
	}

	for(int i = 0; i < n; i ++)
		if(m_pwndMain->m_wndSource.GetCheck(i))
		{
			CString str;

			str = m_pwndMain->m_wndSource.GetItemText(i, 0);
			if(m_pwndMain->m_wndSource.GetItemText(i, 1).IsEmpty())
				str.AppendChar('\\');

			if(strStartup.Compare(str))
				if(!PackOneFile(str))return FALSE;
		}

	if(!m_strLogString.IsEmpty() && !m_pwndLog->WriteLog(m_strLogString))return FALSE;

	return TRUE;
}

BOOL CWorker::EndBuild()
{
	if(!m_pwndLog->WriteLog(_T("\r\n") + GetMessage(L"index") + _T("...\r\n")))return FALSE;

	CZipFile zipFile;

	zipFile.SetKey(m_MD5Pass);

	m_IndexFile.Write(L"", sizeof(WCHAR));
	m_IndexFile.SeekToBegin();
	m_dwPackInfo[1] = (DWORD)m_file.GetPosition();

	zipFile.Open(m_file, CFile::modeWrite);

	char buf[1024];
	long n;

	while(n = m_IndexFile.Read(buf, sizeof(buf)))
		zipFile.Write(buf, n);

	zipFile.Close();

	m_file.Write(m_dwPackInfo, sizeof(m_dwPackInfo));

	m_file.Seek(m_dwPackInfo[0] + MD5_DIGEST_LENGTH, CFile::begin);

	MD5_CTX m_ctxMD5;
	int nSize;
	static char HEX[] = "0123456789ABCDEF";
	char str[41];
	int i, p;

	MD5_Init(&m_ctxMD5);
	do
	{
		nSize = m_file.Read(buf, sizeof(buf));
		MD5_Update(&m_ctxMD5, buf, nSize);
	}while(nSize == sizeof(buf));

	MD5_Final(m_MD5Pass, &m_ctxMD5);

	for(i = 0, p = 0; i < 16; i ++)
	{
		str[p ++] = HEX[m_MD5Pass[i] / 16];
		str[p ++] = HEX[m_MD5Pass[i] % 16];
		if((i == 4) || (i == 6) || (i == 8) || (i == 10))
			str[p ++] = '-';
	}
	str[p] = 0;

	m_file.Close();

	if(!m_pwndLog->WriteLog(_T("\r\nPROG_Info: ")))return FALSE;
	if(!m_pwndLog->WriteLog(str))return FALSE;
	if(!m_pwndLog->WriteLog(_T("\r\n")))return FALSE;

	return TRUE;
}

BOOL CWorker::DoBuild()
{
	if(!StartBuild())return FALSE;
	if(!PackFile())return FALSE;
	if(!EndBuild())return FALSE;

	return TRUE;
}

BOOL CWorker::InitInstance()
{
	Sleep(100);

	m_pwndLog->WriteLog(_T("------------------ ") + GetMessage(L"Starting") + _T(" ------------------\r\n\r\n"));

	if(!DoBuild())
		m_pwndLog->WriteLog(_T("\r\n---------------------- ") + GetMessage(L"optCancel") + _T(" ----------------------\r\n"));
	else
		m_pwndLog->WriteLog(_T("\r\n---------------------- ") + GetMessage(L"done") + _T(" ----------------------\r\n"));

	return FALSE;
}

int CWorker::ExitInstance()
{
	m_pwndLog->PostMessage(WM_COMMAND, IDOK);

	return FALSE;
}

// CWorker message handlers

void CWorker::StartWorker(CLogDialog * pLog)
{
	m_pwndMain = (CNBRDlg *)theApp.m_pMainWnd;
	m_pwndLog = pLog;
	CreateThread();
}

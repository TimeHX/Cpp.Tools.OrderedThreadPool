#include "stdafx.h"
#include "hxOrderedThreadPool.h"
#include<errno.h>
#include <winnt.h>
#include <winbase.h>

using namespace HX_Thread;

#define  WS_WINDOWS

#ifdef WS_WINDOWS

//�����߳�
static unsigned int __stdcall DefaultJobProc(void* lpParameter)
{
	uint32_t pid = GetCurrentThreadId();
	CThreadInfo *pThread = static_cast<CThreadInfo*>(lpParameter);
	assert(pThread);
	COrderedThreadPool *pThreadPoolObj = pThread->m_Pool;
	assert(pThreadPoolObj);
	InterlockedIncrement(&pThreadPoolObj->m_lThreadNum); // ���̸߳���,ԭ�Ӽ�1


	// ����֪ͨ�ź�
	HANDLE hWaitHandle[4];
	hWaitHandle[0] = pThreadPoolObj->m_SemaphoreDel; // ����ɾ���߳�
	hWaitHandle[1] = pThreadPoolObj->m_EventStopThread;// ǿ�ƹر������߳�(���������Ƿ�����)
	hWaitHandle[2] = pThreadPoolObj->m_EventWaitStopThread;// ��ر������߳�(�ȴ���������)
	hWaitHandle[3] = pThreadPoolObj->m_SemaphoreCall; // ����ִ������
	CThreadTask* pJob = NULL;
	bool stopthis = false; // �ر��߳�
	for (;;)
	{
		pJob = NULL; //��ֹ�ظ��ͷ�
					 // �����ź���m_SemaphoreDel�� waitÿ�μ�1,ֻҪ����0�Ͳ�����,�Ӷ����ٶ���߳�
					 // �����ź���m_SemaphoreCall��waitÿ�μ�1,ֻҪ����0�Ͳ��������Ӷ��ᰴ�������������N��
					 // �����¼�m_EventStopThread������wait�������������߳�ֱ���˳�
					 // �����¼�m_EventWaitStopThread�������̼߳�������Ƿ���������ǣ�ֱ���˳��������������
		DWORD wr = WaitForMultipleObjects(4, hWaitHandle, false, INFINITE);
		//��Ӧɾ���߳��ź�
		if (wr == WAIT_OBJECT_0) { break; }
		// ǿ�ƹر������߳�
		if (wr == WAIT_OBJECT_0 + 1) { break; }
		// ��ر������߳�
		if (wr == WAIT_OBJECT_0 + 2) { stopthis = true; }
		//�Ӷ�����ȡ������
		bool isempty = true;
		pThreadPoolObj->m_mxTaskLock.lock();// ������
		isempty = pThreadPoolObj->m_TaskMap.empty();
		if (!isempty)
		{
			std::map<int, std::queue<CThreadTask*>>::iterator item = pThreadPoolObj->m_TaskMap.begin();
			for (; item != pThreadPoolObj->m_TaskMap.end(); item++)
			{
				if(item->second.empty()) continue;
				pJob = item->second.front();
				//�Ƿ�׼��ִ��״̬
				if (pJob->IsRun())
				{
					pJob = NULL;
					continue;
				}
				else if (pJob->IsReadyRun())
				{
					pJob->Run();
					break;
				}
				//�Ƿ�ִ�н���/��
				else if (pJob->IsStop() || pJob->IsEmpty()) {
					delete pJob; pJob = NULL;
					item->second.pop();
					continue;
				}
				else {
					pJob = NULL;
				}
			}
		}
		pThreadPoolObj->m_mxTaskLock.unlock();// ����
		// ��������������˳����˳�
		if (stopthis && isempty) { break; }
		// ִ��
		if (pJob)
		{
			InterlockedIncrement(&pThreadPoolObj->m_lRunningNum); // �������е��̸߳��� +1
			pThread->m_StartTime = GetTickCount();
			pThread->m_Count++;
			pThread->m_IsRunning = true;

			//��������
			HX_Thread::WS_THREAD_FUNCTION pFun = pJob->GetFun();
			if (pFun)
				pFun(pJob->GetParam());
			
			pThread->m_IsRunning = false;
			InterlockedDecrement(&pThreadPoolObj->m_lRunningNum); // �������е��̸߳��� -1

			//ִ�н�����־λ����
			pThreadPoolObj->m_mxTaskLock.lock();
			pJob->Stop();
			pThreadPoolObj->m_mxTaskLock.unlock();
		}
		//Sleep(1);
	}
	//ɾ������ṹ
	pThreadPoolObj->m_mxThreadLock.lock();
	pThreadPoolObj->m_ThreadVector.erase(find(pThreadPoolObj->m_ThreadVector.begin(), pThreadPoolObj->m_ThreadVector.end(), pThread)); // ɾ���߳�
	pThreadPoolObj->m_mxThreadLock.unlock();
	delete pThread;
	InterlockedDecrement(&pThreadPoolObj->m_lThreadNum); // ���̸߳���,ԭ�� -1
														 //�����߳̽���
	if (0 >= pThreadPoolObj->m_lThreadNum)
	{
		SetEvent(pThreadPoolObj->m_EventPool);
	}
	return 0;
}


COrderedThreadPool::COrderedThreadPool(uint32_t dwNum)
{
	m_lThreadNum = 0;
	// ���߳���
	m_lRunningNum = 0;
	// ��ǰ�������е��߳���
	// ��ʼ���ź���
	m_SemaphoreCall = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL); // �����̴߳�������,һ�ο��Ի��Ѷ��,0x7FFFFFFF��ʾ������������������������
	m_SemaphoreDel = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL);  // �����̲߳�ɾ��,һ�ο��Ի��Ѷ��, 0x7FFFFFFF��ʾ������������ɾ�����߳�����
	// ��ʼ���¼��ں˶���
	m_EventStopThread = CreateEvent(0, false, false, NULL);
	// Ӳ�ر������߳� 
	m_EventWaitStopThread = CreateEvent(0, true, false, NULL); // ��ر������߳�
	m_EventPool = CreateEvent(0, true, false, NULL); // �ر��̳߳�


	// ��鴴���ľ���Ƿ���Ч
	assert(m_SemaphoreCall != INVALID_HANDLE_VALUE);
	assert(m_SemaphoreDel != INVALID_HANDLE_VALUE);
	assert(m_EventStopThread != INVALID_HANDLE_VALUE);
	assert(m_EventWaitStopThread != INVALID_HANDLE_VALUE);
	assert(m_EventPool != INVALID_HANDLE_VALUE);
	updateThreadNum(dwNum);
}

COrderedThreadPool::~COrderedThreadPool()
{
	std::vector<CThreadInfo*>::iterator iter = m_ThreadVector.begin();
	for (; iter != m_ThreadVector.end(); ++iter)
	{
		if (*iter) { delete *iter; }

	}

	for ( auto item : m_TaskMap)
	{
		while (!(item.second.empty()))
		{
			CThreadTask* pJob = item.second.front();
			if (pJob) delete pJob;
			item.second.pop();
		}
	}

	// �����¼�,�ٽ���,�ź���
	CloseHandle(m_SemaphoreCall);
	CloseHandle(m_SemaphoreDel);
	CloseHandle(m_EventStopThread);
	CloseHandle(m_EventWaitStopThread);
	CloseHandle(m_EventPool);
}

uint32_t COrderedThreadPool::updateThreadNum(int32_t aNum)
{
	if (aNum > 0)
	{
		m_mxThreadLock.lock();// ����
		for (int32_t i = 0; i < aNum; ++i)
		{
			CThreadInfo *pNewThread = new CThreadInfo(); // ���߳�
			pNewThread->m_Pool = this;
			assert(pNewThread);
			// ���ﲻ�������������߳�
			HANDLE pId = pNewThread->m_ThreadObj.CreateThread(pNewThread, (WS_THREAD_FUN)DefaultJobProc); // �����߳�
			assert(pId != 0);
			m_ThreadVector.push_back(pNewThread);
			// ���뵽����
		}
		m_mxThreadLock.unlock();// ����
	}
	else
	{
		// �ж�
		aNum = 1;
		ReleaseSemaphore(m_SemaphoreDel, ((uint32_t)aNum > m_lThreadNum) ? m_lThreadNum : aNum, NULL); // �ź���������ָ�����
	}


	return m_lThreadNum;
}

bool COrderedThreadPool::addTheadTask(int nSessionID, WS_THREAD_FUNCTION pFun, void* pObj)
{
	assert(pFun);
	CThreadTask* task = new CThreadTask(pFun, pObj);
	m_mxTaskLock.lock();
	std::map<int, std::queue<CThreadTask*>>::iterator it = m_TaskMap.find(nSessionID);
	if (it != m_TaskMap.end())
	{
		if (m_TaskMap[nSessionID].size() <= WS_THREAD_POOL_TASK_MAX)
		{
			m_TaskMap[nSessionID].push(task);
			m_mxTaskLock.unlock();
			ReleaseSemaphore(m_SemaphoreCall, 1, NULL); // �ź�֪ͨ
			return true;
		}
	}
	else {
		m_TaskMap[nSessionID].push(task);
		m_mxTaskLock.unlock();
		ReleaseSemaphore(m_SemaphoreCall, 1, NULL); // �ź�֪ͨ
		return true;
	}
	
	m_mxTaskLock.unlock();
	return false;
}

bool COrderedThreadPool::EndAndWait()
{
	SetEvent(m_EventWaitStopThread);
	return WaitForSingleObject(m_EventPool, INFINITE) == WAIT_OBJECT_0;
}

bool COrderedThreadPool::End()
{
	SetEvent(m_EventStopThread);
	return WaitForSingleObject(m_EventPool, INFINITE) == WAIT_OBJECT_0;
}

uint32_t COrderedThreadPool::GetThreadNum()
{
	return m_lThreadNum;
}

uint32_t COrderedThreadPool::GetRunningThreadNum()
{
	return m_lRunningNum;
}

bool COrderedThreadPool::IsRunning()
{
	return m_lThreadNum > 0;
}

uint32_t COrderedThreadPool::GetTaskNum(int nSessionID)
{
	return m_TaskMap[nSessionID].size();
}


#else


//�����߳�
static void* DefaultJobProc(void* lpParameter)
{
	CThreadInfo *pThread = static_cast<CThreadInfo*>(lpParameter);
	assert(pThread);
	COrderedThreadPool *pThreadPoolObj = pThread->m_Pool;
	assert(pThreadPoolObj);
	__sync_add_and_fetch(&pThreadPoolObj->m_lThreadNum, 1); // ���̸߳���,ԭ�Ӽ�1
	struct timeval now;
	struct timespec outtime;
	ThreadTask* pJob = NULL;
	bool stopthis = false; // �ر��߳�
	for (;;)
	{
		pJob = NULL; //��ֹ�ظ��ͷ�
		gettimeofday(&now, NULL);
		outtime.tv_sec = now.tv_sec + 1;
		outtime.tv_nsec = now.tv_usec;
		// �ź���m_SemaphoreCall�� waitÿ�μ�1,ֻҪ����0�Ͳ�����,�Ӷ����ٶ���߳�,֧�ֳ�ʱ1����
		int ret = sem_timedwait(&pThreadPoolObj->m_SemaphoreCall, &outtime);
		if ((ret == -1) && (ETIMEDOUT != errno)) // �쳣����
		{
			gettimeofday(&now, NULL);
			WS_LOG(Error, "thread_pool_sem_timedwait_fail! thread_id:%u, error:%d", pthread_self(), errno);
			cout << "sem_timewait timeout " << now.tv_sec << " " << (now.tv_usec) << "\n\n";
		}


		// ǿ��ɾ���߳�
		if (pThreadPoolObj->m_EventStopThread > 0) // ������Ҫ��
		{
			int32_t ret = __sync_fetch_and_sub(&(pThreadPoolObj->m_EventStopThread), -1); // ���ؼ�֮ǰ�Ľ��
			if (ret > 0) { break; }
		}


		// ��ر������߳�
		if (pThreadPoolObj->m_EventWaitStopThread > 0)
		{
			int32_t ret = __sync_fetch_and_sub(&(pThreadPoolObj->m_EventStopThread), -1); // ���ؼ�֮ǰ�Ľ��
			if (ret > 0) { stopthis = true; }
		}
		//�Ӷ�����ȡ������
		bool isempty = true;
		pthread_mutex_lock(&pThreadPoolObj->m_TaskLock);// ������
		isempty = pThreadPoolObj->m_TaskQueue.empty();
		if (!isempty)
		{
			pJob = pThreadPoolObj->m_TaskQueue.front(); // ��ȡ����
			pThreadPoolObj->m_TaskQueue.pop();
		}
		pthread_mutex_unlock(&pThreadPoolObj->m_TaskLock); // ����
														   // ��������������˳����˳�
		if (stopthis && isempty) { break; }
		// ִ��
		if (pJob)
		{
			__sync_fetch_and_add(&pThreadPoolObj->m_lRunningNum, 1); // �������е��̸߳��� +1
			pThread->m_StartTime = time(NULL);
			pThread->m_Count++;
			pThread->m_IsRunning = true;
			pJob->m_fun(pJob->m_para); //��������
			delete pJob;
			pThread->m_IsRunning = false;
			__sync_fetch_and_sub(&pThreadPoolObj->m_lRunningNum, 1); // �������е��̸߳��� -1
		}
	}
	//ɾ������ṹ
	pthread_mutex_lock(&pThreadPoolObj->m_ThreadLock);
	pThreadPoolObj->m_ThreadVector.erase(find(pThreadPoolObj->m_ThreadVector.begin(), pThreadPoolObj->m_ThreadVector.end(), pThread)); // ɾ���߳�
	pthread_mutex_unlock(&pThreadPoolObj->m_ThreadLock);
	delete pThread;
	int ret = __sync_fetch_and_sub(&pThreadPoolObj->m_lThreadNum, 1); // ���̸߳���,ԭ�� -1
	if (ret <= 1)
	{
		sem_post(&pThreadPoolObj->m_SemaphoreDel); // ֪ͨ�̳߳عر�
	}


	return 0;
}






// ���캯��
COrderedThreadPool::COrderedThreadPool(uint32_t dwNum)
{
	m_lThreadNum = 0;
	// ���߳���
	m_lRunningNum = 0;
	// ��ǰ�������е��߳���
	// ��ʼ����
	assert(0 == pthread_mutex_init(&m_ThreadLock, NULL)); // ��ʼ���̶߳�����
	assert(0 == pthread_mutex_init(&m_TaskLock, NULL)); // ��ʼ�����������
														// ��ʼ���ź���
	assert(0 == sem_init(&m_SemaphoreCall, 0, 0));
	assert(0 == sem_init(&m_SemaphoreDel, 0, 0));
	// ��ʼ������ֵ
	m_EventStopThread = 0; // n��ʾ��Ҫ�رյ��߳�����
	m_EventWaitStopThread = 0; // n��ʾ��Ҫ�رյ��߳�����
	m_EventPool = 0; // ��ʱ����
	updateThreadNum(dwNum);
}


// ��������
COrderedThreadPool::~COrderedThreadPool()
{
	vector<CThreadInfo*>::iterator iter = m_ThreadVector.begin();
	for (; iter != m_ThreadVector.end(); ++iter)
	{
		if (*iter) { delete *iter; }
	}
	// �����¼�,�ٽ���,�ź���
	sem_destroy(&m_SemaphoreCall);
	sem_destroy(&m_SemaphoreDel);
	m_EventStopThread = 0; // n��ʾ��Ҫ�رյ��߳�����
	m_EventWaitStopThread = 0; // n��ʾ��Ҫ�رյ��߳�����
	m_EventPool = 0; // ��ʱ����
	pthread_mutex_destroy(&m_TaskLock); // �������������
	pthread_mutex_destroy(&m_ThreadLock); // �����̶߳�����
}




//�����̳߳ع�ģ
uint32_t COrderedThreadPool::updateThreadNum(int32_t aNum)
{
	if (aNum > 0)
	{
		pthread_mutex_lock(&m_ThreadLock);// ����
		for (int32_t i = 0; i < aNum; ++i)
		{
			CThreadInfo *pNewThread = new CThreadInfo(); // ���߳�
			pNewThread->m_Pool = this;
			assert(pNewThread);
			// ���ﲻ������Ϊ���̻߳���
			//pNewThread->m_ThreadObj.setThreadIsDetached(false); // ���̻߳���
			WS_THREAD_HANDLE pId = pNewThread->m_ThreadObj.CreateThread(pNewThread, DefaultJobProc); // �����߳�
			assert(pId != 0);
			m_ThreadVector.push_back(pNewThread);
			// ���뵽����
		}
		pthread_mutex_unlock(&m_ThreadLock); // ����
	}
	else
	{
		// �ж�
		aNum *= -1;
		aNum = aNum > m_lThreadNum ? m_lThreadNum : aNum;
		__sync_lock_test_and_set(&m_EventStopThread, aNum); // ���ý�Ҫ�������̳߳����̵߳ĸ���
	}


	return m_lThreadNum;
}


// ��������������
bool COrderedThreadPool::addTheadTask(WS_THREAD_FUNCTION pFun, void* pObj)
{
	assert(pFun);
	if (m_TaskQueue.size() <= WS_THREAD_POOL_TASK_MAX)
	{
		ThreadTask* task = new ThreadTask(pFun, pObj);
		pthread_mutex_lock(&m_TaskLock);
		m_TaskQueue.push(task);
		pthread_mutex_unlock(&m_TaskLock);
		sem_post(&m_SemaphoreCall);
		//WS_LOG(Info, "thread_pool_task_size size:%u", m_TaskQueue.size());
		return true;
	}


	WS_LOG(Error, "thread_pool_task_size have too match! size:%u", m_TaskQueue.size());
	return false;
}



//�����̳߳�, ��ͬ���ȴ�
bool COrderedThreadPool::EndAndWait()
{
	__sync_lock_test_and_set(&m_EventWaitStopThread, m_lThreadNum + 1); // �ر��߳�����
	struct timeval now;
	struct timespec outtime;
	gettimeofday(&now, NULL);
	outtime.tv_sec = now.tv_sec + 2 * m_lThreadNum; // ��������������Ҫ��ʱ��
	outtime.tv_nsec = now.tv_usec;
	int ret = sem_timedwait(&m_SemaphoreDel, &outtime);
	if (ret == -1) { return false; }

	return true;
}


//�����̳߳�
bool COrderedThreadPool::End()
{
	__sync_lock_test_and_set(&m_EventStopThread, m_lThreadNum + 1); // �ر��߳�����
	struct timeval now;
	struct timespec outtime;
	gettimeofday(&now, NULL);
	outtime.tv_sec = now.tv_sec + 1 * m_lThreadNum; // ��������������Ҫ��ʱ��
	outtime.tv_nsec = now.tv_usec;
	int ret = sem_timedwait(&m_SemaphoreDel, &outtime);
	if (ret == -1) { return false; }
	return true;
}


// �߳�����
uint32_t COrderedThreadPool::GetThreadNum()
{
	return m_lThreadNum;
}


// �����е��߳�����
uint32_t COrderedThreadPool::GetRunningThreadNum()
{
	return m_lRunningNum;
}


// �ж��ǲ���������
bool COrderedThreadPool::IsRunning()
{
	return m_lThreadNum > 0;
}


// ��ȡ�������������
uint32_t COrderedThreadPool::GetTaskNum()
{
	return m_TaskQueue.size();
}


#endif


HX_Thread::CThreadTask::CThreadTask(HX_Thread::WS_THREAD_FUNCTION afun, void *pPara) :m_fun(afun), m_para(pPara), m_byStatus(E_READY_RUN)
{

}

HX_Thread::CThreadTask::CThreadTask() : m_fun(NULL), m_para(NULL), m_byStatus(E_EMPTY)
{

}

HX_Thread::CThreadTask::CThreadTask(HX_Thread::WS_THREAD_FUNCTION afun) : m_fun(afun), m_para(NULL), m_byStatus(E_READY_RUN)
{

}

void HX_Thread::CThreadTask::ReadyRun()
{
	m_byStatus = E_READY_RUN;
}

void HX_Thread::CThreadTask::Run()
{
	m_byStatus = E_RUNING;
}

void HX_Thread::CThreadTask::Stop()
{
	m_byStatus = E_STOP;
}

void HX_Thread::CThreadTask::Clear()
{
	m_byStatus = E_EMPTY;
}

bool HX_Thread::CThreadTask::IsReadyRun()
{
	return m_byStatus == E_READY_RUN;
}

bool HX_Thread::CThreadTask::IsRun()
{
	return m_byStatus == E_RUNING;
}

bool HX_Thread::CThreadTask::IsStop()
{
	return m_byStatus == E_STOP;
}

bool HX_Thread::CThreadTask::IsEmpty()
{
	return m_byStatus == E_EMPTY;
}

unsigned char HX_Thread::CThreadTask::GetStatus()
{
	return m_byStatus;
}

void HX_Thread::CThreadTask::SetFun(HX_Thread::WS_THREAD_FUNCTION afun /*= NULL*/)
{
	m_fun = afun;
}

HX_Thread::WS_THREAD_FUNCTION HX_Thread::CThreadTask::GetFun()
{
	return m_fun;
}

void HX_Thread::CThreadTask::SetParam(void*param /*= NULL*/)
{
	m_para = param;
}

void* HX_Thread::CThreadTask::GetParam()
{
	return m_para;
}

HANDLE HX_Thread::CThread::CreateThread(CThreadInfo * pInfo, WS_THREAD_FUN pfun)
{
	m_pInfo = pInfo;
	m_pFun = (LPTHREAD_START_ROUTINE)pfun;
	return m_handle = ::CreateThread(NULL, 0, m_pFun, pInfo, 0, &m_threadId);
}

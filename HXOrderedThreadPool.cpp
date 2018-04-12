#include "stdafx.h"
#include "hxOrderedThreadPool.h"
#include<errno.h>
#include <winnt.h>
#include <winbase.h>

using namespace HX_Thread;

#define  WS_WINDOWS

#ifdef WS_WINDOWS

//工作线程
static unsigned int __stdcall DefaultJobProc(void* lpParameter)
{
	uint32_t pid = GetCurrentThreadId();
	CThreadInfo *pThread = static_cast<CThreadInfo*>(lpParameter);
	assert(pThread);
	COrderedThreadPool *pThreadPoolObj = pThread->m_Pool;
	assert(pThreadPoolObj);
	InterlockedIncrement(&pThreadPoolObj->m_lThreadNum); // 总线程个数,原子加1


	// 创建通知信号
	HANDLE hWaitHandle[4];
	hWaitHandle[0] = pThreadPoolObj->m_SemaphoreDel; // 唤醒删除线程
	hWaitHandle[1] = pThreadPoolObj->m_EventStopThread;// 强制关闭所有线程(不管任务是否处理完)
	hWaitHandle[2] = pThreadPoolObj->m_EventWaitStopThread;// 软关闭所有线程(等待任务处理完)
	hWaitHandle[3] = pThreadPoolObj->m_SemaphoreCall; // 唤醒执行任务
	CThreadTask* pJob = NULL;
	bool stopthis = false; // 关闭线程
	for (;;)
	{
		pJob = NULL; //防止重复释放
					 // 若是信号量m_SemaphoreDel： wait每次减1,只要大于0就不阻塞,从而销毁多个线程
					 // 若是信号量m_SemaphoreCall：wait每次减1,只要大于0就不阻塞，从而会按照任务个数调度N次
					 // 若是事件m_EventStopThread：所有wait都不在阻塞，线程直接退出
					 // 若是事件m_EventWaitStopThread：所有线程检查任务是否结束，如是，直接退出，否则继续处理
		DWORD wr = WaitForMultipleObjects(4, hWaitHandle, false, INFINITE);
		//响应删除线程信号
		if (wr == WAIT_OBJECT_0) { break; }
		// 强制关闭所有线程
		if (wr == WAIT_OBJECT_0 + 1) { break; }
		// 软关闭所有线程
		if (wr == WAIT_OBJECT_0 + 2) { stopthis = true; }
		//从队列里取出任务
		bool isempty = true;
		pThreadPoolObj->m_mxTaskLock.lock();// 队列锁
		isempty = pThreadPoolObj->m_TaskMap.empty();
		if (!isempty)
		{
			std::map<int, std::queue<CThreadTask*>>::iterator item = pThreadPoolObj->m_TaskMap.begin();
			for (; item != pThreadPoolObj->m_TaskMap.end(); item++)
			{
				if(item->second.empty()) continue;
				pJob = item->second.front();
				//是否准备执行状态
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
				//是否执行结束/空
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
		pThreadPoolObj->m_mxTaskLock.unlock();// 解锁
		// 无任务，如果有软退出就退出
		if (stopthis && isempty) { break; }
		// 执行
		if (pJob)
		{
			InterlockedIncrement(&pThreadPoolObj->m_lRunningNum); // 正在运行的线程个数 +1
			pThread->m_StartTime = GetTickCount();
			pThread->m_Count++;
			pThread->m_IsRunning = true;

			//处理任务
			HX_Thread::WS_THREAD_FUNCTION pFun = pJob->GetFun();
			if (pFun)
				pFun(pJob->GetParam());
			
			pThread->m_IsRunning = false;
			InterlockedDecrement(&pThreadPoolObj->m_lRunningNum); // 正在运行的线程个数 -1

			//执行结束标志位设置
			pThreadPoolObj->m_mxTaskLock.lock();
			pJob->Stop();
			pThreadPoolObj->m_mxTaskLock.unlock();
		}
		//Sleep(1);
	}
	//删除自身结构
	pThreadPoolObj->m_mxThreadLock.lock();
	pThreadPoolObj->m_ThreadVector.erase(find(pThreadPoolObj->m_ThreadVector.begin(), pThreadPoolObj->m_ThreadVector.end(), pThread)); // 删除线程
	pThreadPoolObj->m_mxThreadLock.unlock();
	delete pThread;
	InterlockedDecrement(&pThreadPoolObj->m_lThreadNum); // 总线程个数,原子 -1
														 //所有线程结束
	if (0 >= pThreadPoolObj->m_lThreadNum)
	{
		SetEvent(pThreadPoolObj->m_EventPool);
	}
	return 0;
}


COrderedThreadPool::COrderedThreadPool(uint32_t dwNum)
{
	m_lThreadNum = 0;
	// 总线程数
	m_lRunningNum = 0;
	// 当前正在运行的线程数
	// 初始化信号量
	m_SemaphoreCall = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL); // 唤醒线程处理任务,一次可以唤醒多个,0x7FFFFFFF表示可阻塞的最大待处理任务数量
	m_SemaphoreDel = CreateSemaphore(0, 0, 0x7FFFFFFF, NULL);  // 唤醒线程并删除,一次可以唤醒多个, 0x7FFFFFFF表示可阻塞的最大可删除的线程数量
	// 初始化事件内核对象
	m_EventStopThread = CreateEvent(0, false, false, NULL);
	// 硬关闭所有线程 
	m_EventWaitStopThread = CreateEvent(0, true, false, NULL); // 软关闭所有线程
	m_EventPool = CreateEvent(0, true, false, NULL); // 关闭线程池


	// 检查创建的句柄是否有效
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

	// 销毁事件,临界区,信号量
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
		m_mxThreadLock.lock();// 加锁
		for (int32_t i = 0; i < aNum; ++i)
		{
			CThreadInfo *pNewThread = new CThreadInfo(); // 新线程
			pNewThread->m_Pool = this;
			assert(pNewThread);
			// 这里不能设置阻塞父线程
			HANDLE pId = pNewThread->m_ThreadObj.CreateThread(pNewThread, (WS_THREAD_FUN)DefaultJobProc); // 创建线程
			assert(pId != 0);
			m_ThreadVector.push_back(pNewThread);
			// 加入到队列
		}
		m_mxThreadLock.unlock();// 解锁
	}
	else
	{
		// 判断
		aNum = 1;
		ReleaseSemaphore(m_SemaphoreDel, ((uint32_t)aNum > m_lThreadNum) ? m_lThreadNum : aNum, NULL); // 信号量计数加指定多个
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
			ReleaseSemaphore(m_SemaphoreCall, 1, NULL); // 信号通知
			return true;
		}
	}
	else {
		m_TaskMap[nSessionID].push(task);
		m_mxTaskLock.unlock();
		ReleaseSemaphore(m_SemaphoreCall, 1, NULL); // 信号通知
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


//工作线程
static void* DefaultJobProc(void* lpParameter)
{
	CThreadInfo *pThread = static_cast<CThreadInfo*>(lpParameter);
	assert(pThread);
	COrderedThreadPool *pThreadPoolObj = pThread->m_Pool;
	assert(pThreadPoolObj);
	__sync_add_and_fetch(&pThreadPoolObj->m_lThreadNum, 1); // 总线程个数,原子加1
	struct timeval now;
	struct timespec outtime;
	ThreadTask* pJob = NULL;
	bool stopthis = false; // 关闭线程
	for (;;)
	{
		pJob = NULL; //防止重复释放
		gettimeofday(&now, NULL);
		outtime.tv_sec = now.tv_sec + 1;
		outtime.tv_nsec = now.tv_usec;
		// 信号量m_SemaphoreCall： wait每次减1,只要大于0就不阻塞,从而销毁多个线程,支持超时1秒钟
		int ret = sem_timedwait(&pThreadPoolObj->m_SemaphoreCall, &outtime);
		if ((ret == -1) && (ETIMEDOUT != errno)) // 异常处理
		{
			gettimeofday(&now, NULL);
			WS_LOG(Error, "thread_pool_sem_timedwait_fail! thread_id:%u, error:%d", pthread_self(), errno);
			cout << "sem_timewait timeout " << now.tv_sec << " " << (now.tv_usec) << "\n\n";
		}


		// 强制删除线程
		if (pThreadPoolObj->m_EventStopThread > 0) // 并不需要锁
		{
			int32_t ret = __sync_fetch_and_sub(&(pThreadPoolObj->m_EventStopThread), -1); // 返回减之前的结果
			if (ret > 0) { break; }
		}


		// 软关闭所有线程
		if (pThreadPoolObj->m_EventWaitStopThread > 0)
		{
			int32_t ret = __sync_fetch_and_sub(&(pThreadPoolObj->m_EventStopThread), -1); // 返回减之前的结果
			if (ret > 0) { stopthis = true; }
		}
		//从队列里取出任务
		bool isempty = true;
		pthread_mutex_lock(&pThreadPoolObj->m_TaskLock);// 队列锁
		isempty = pThreadPoolObj->m_TaskQueue.empty();
		if (!isempty)
		{
			pJob = pThreadPoolObj->m_TaskQueue.front(); // 获取任务
			pThreadPoolObj->m_TaskQueue.pop();
		}
		pthread_mutex_unlock(&pThreadPoolObj->m_TaskLock); // 解锁
														   // 无任务，如果有软退出就退出
		if (stopthis && isempty) { break; }
		// 执行
		if (pJob)
		{
			__sync_fetch_and_add(&pThreadPoolObj->m_lRunningNum, 1); // 正在运行的线程个数 +1
			pThread->m_StartTime = time(NULL);
			pThread->m_Count++;
			pThread->m_IsRunning = true;
			pJob->m_fun(pJob->m_para); //处理任务
			delete pJob;
			pThread->m_IsRunning = false;
			__sync_fetch_and_sub(&pThreadPoolObj->m_lRunningNum, 1); // 正在运行的线程个数 -1
		}
	}
	//删除自身结构
	pthread_mutex_lock(&pThreadPoolObj->m_ThreadLock);
	pThreadPoolObj->m_ThreadVector.erase(find(pThreadPoolObj->m_ThreadVector.begin(), pThreadPoolObj->m_ThreadVector.end(), pThread)); // 删除线程
	pthread_mutex_unlock(&pThreadPoolObj->m_ThreadLock);
	delete pThread;
	int ret = __sync_fetch_and_sub(&pThreadPoolObj->m_lThreadNum, 1); // 总线程个数,原子 -1
	if (ret <= 1)
	{
		sem_post(&pThreadPoolObj->m_SemaphoreDel); // 通知线程池关闭
	}


	return 0;
}






// 构造函数
COrderedThreadPool::COrderedThreadPool(uint32_t dwNum)
{
	m_lThreadNum = 0;
	// 总线程数
	m_lRunningNum = 0;
	// 当前正在运行的线程数
	// 初始化锁
	assert(0 == pthread_mutex_init(&m_ThreadLock, NULL)); // 初始化线程队列锁
	assert(0 == pthread_mutex_init(&m_TaskLock, NULL)); // 初始化任务队列锁
														// 初始化信号量
	assert(0 == sem_init(&m_SemaphoreCall, 0, 0));
	assert(0 == sem_init(&m_SemaphoreDel, 0, 0));
	// 初始化控制值
	m_EventStopThread = 0; // n表示需要关闭的线程数量
	m_EventWaitStopThread = 0; // n表示需要关闭的线程数量
	m_EventPool = 0; // 暂时不用
	updateThreadNum(dwNum);
}


// 析构函数
COrderedThreadPool::~COrderedThreadPool()
{
	vector<CThreadInfo*>::iterator iter = m_ThreadVector.begin();
	for (; iter != m_ThreadVector.end(); ++iter)
	{
		if (*iter) { delete *iter; }
	}
	// 销毁事件,临界区,信号量
	sem_destroy(&m_SemaphoreCall);
	sem_destroy(&m_SemaphoreDel);
	m_EventStopThread = 0; // n表示需要关闭的线程数量
	m_EventWaitStopThread = 0; // n表示需要关闭的线程数量
	m_EventPool = 0; // 暂时不用
	pthread_mutex_destroy(&m_TaskLock); // 销毁任务队列锁
	pthread_mutex_destroy(&m_ThreadLock); // 销毁线程队列锁
}




//调整线程池规模
uint32_t COrderedThreadPool::updateThreadNum(int32_t aNum)
{
	if (aNum > 0)
	{
		pthread_mutex_lock(&m_ThreadLock);// 加锁
		for (int32_t i = 0; i < aNum; ++i)
		{
			CThreadInfo *pNewThread = new CThreadInfo(); // 新线程
			pNewThread->m_Pool = this;
			assert(pNewThread);
			// 这里不能设置为父线程回收
			//pNewThread->m_ThreadObj.setThreadIsDetached(false); // 父线程回收
			WS_THREAD_HANDLE pId = pNewThread->m_ThreadObj.CreateThread(pNewThread, DefaultJobProc); // 创建线程
			assert(pId != 0);
			m_ThreadVector.push_back(pNewThread);
			// 加入到队列
		}
		pthread_mutex_unlock(&m_ThreadLock); // 解锁
	}
	else
	{
		// 判断
		aNum *= -1;
		aNum = aNum > m_lThreadNum ? m_lThreadNum : aNum;
		__sync_lock_test_and_set(&m_EventStopThread, aNum); // 设置将要消减的线程池中线程的个数
	}


	return m_lThreadNum;
}


// 添加任务到任务队列
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



//结束线程池, 并同步等待
bool COrderedThreadPool::EndAndWait()
{
	__sync_lock_test_and_set(&m_EventWaitStopThread, m_lThreadNum + 1); // 关闭线程设置
	struct timeval now;
	struct timespec outtime;
	gettimeofday(&now, NULL);
	outtime.tv_sec = now.tv_sec + 2 * m_lThreadNum; // 尽量大于销毁需要的时间
	outtime.tv_nsec = now.tv_usec;
	int ret = sem_timedwait(&m_SemaphoreDel, &outtime);
	if (ret == -1) { return false; }

	return true;
}


//结束线程池
bool COrderedThreadPool::End()
{
	__sync_lock_test_and_set(&m_EventStopThread, m_lThreadNum + 1); // 关闭线程设置
	struct timeval now;
	struct timespec outtime;
	gettimeofday(&now, NULL);
	outtime.tv_sec = now.tv_sec + 1 * m_lThreadNum; // 尽量大于销毁需要的时间
	outtime.tv_nsec = now.tv_usec;
	int ret = sem_timedwait(&m_SemaphoreDel, &outtime);
	if (ret == -1) { return false; }
	return true;
}


// 线程总数
uint32_t COrderedThreadPool::GetThreadNum()
{
	return m_lThreadNum;
}


// 运行中的线程数量
uint32_t COrderedThreadPool::GetRunningThreadNum()
{
	return m_lRunningNum;
}


// 判断是不是运行中
bool COrderedThreadPool::IsRunning()
{
	return m_lThreadNum > 0;
}


// 获取阻塞的任务个数
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

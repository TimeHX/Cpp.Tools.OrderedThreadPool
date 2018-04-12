/*!
* \brief 有序线程池  多用户多线程
*
* TODO: 
*
* \note 有序线程池
*
* \author TIME
*
* \version 1.0
*
* \date 四月 2018
*
* Contact: TimeHX@hxonline.tv
*
*/
#ifndef _CONDITION_H_
#define _CONDITION_H_
#include <cassert>
#include <vector>
#include <queue>
#include <map>
#include <wtypes.h>
#include <thread>
#include "sync.h"

namespace HX_Thread
{
	class COrderedThreadPool;
	class CThreadTask;
	class CThread;
	class CThreadInfo;
#define WS_THREAD_POOL_TASK_MAX (9999999) // 同时允许存在的任务数为9999999个
	typedef void (*WS_THREAD_FUNCTION)(void*);
	typedef unsigned int(*WS_THREAD_FUN)(void*);


	//////////////////////////////////////////////////////////////////////////
	///>> Task(任务)
	class CThreadTask
	{
		enum {
			E_EMPTY,		// 空任务
			E_READY_RUN,	// 等待执行
			E_RUNING,		// 执行中
			E_STOP			// 执行结束
		};
	public:
		CThreadTask();
		CThreadTask(HX_Thread::WS_THREAD_FUNCTION afun);
		CThreadTask(HX_Thread::WS_THREAD_FUNCTION afun, void *pPara);
		
		//************************************
		// 备注:      
		// 函数名:    ReadyRun
		// 函数全名:  HX_Thread::CThreadTask::ReadyRun
		// 访问权限:  public 
		// 返回值:    void
		// 说明:     任务等待被执行
		//************************************
		void ReadyRun();

		//************************************
		// 备注:      
		// 函数名:    Run
		// 函数全名:  HX_Thread::CThreadTask::Run
		// 访问权限:  public 
		// 返回值:    void
		// 说明:     任务执行中
		//************************************
		void Run();

		//************************************
		// 备注:      
		// 函数名:    Stop
		// 函数全名:  HX_Thread::CThreadTask::Stop
		// 访问权限:  public 
		// 返回值:    void
		// 说明:     任务执行结束
		//************************************
		void Stop();

		//************************************
		// 备注:      
		// 函数名:    Clear
		// 函数全名:  HX_Thread::CThreadTask::Clear
		// 访问权限:  public 
		// 返回值:    void
		// 说明:     清空任务
		//************************************
		void Clear();


		bool IsReadyRun();
		bool IsRun();
		bool IsStop();
		bool IsEmpty();
		unsigned char GetStatus();


		void SetFun(HX_Thread::WS_THREAD_FUNCTION afun = NULL);
		HX_Thread::WS_THREAD_FUNCTION GetFun();


		void SetParam(void*param = NULL);
		void* GetParam();

	private:
		unsigned char		m_byStatus;		// 当前线程状态
		WS_THREAD_FUNCTION	m_fun;			// 函数
		void*				m_para;			// 参数
	};

	//////////////////////////////////////////////////////////////////////////
	///>>线程
	class CThread
	{
	public:
		CThread() :m_pInfo(NULL), m_pFun(NULL) {}
		~CThread() { CloseHandle(m_handle); }

		HANDLE CreateThread(CThreadInfo * pInfo, WS_THREAD_FUN pfun);
	protected:
		
	private:
		CThreadInfo * m_pInfo;
		LPTHREAD_START_ROUTINE m_pFun;
		HANDLE  m_handle;
		DWORD   m_threadId;
	};

	//////////////////////////////////////////////////////////////////////////
	///>> 线程信息
	class CThreadInfo
	{
	public:
		
		CThread m_ThreadObj;			// 线程对象
		uint64_t m_StartTime;		// 最后一次运行开始时间
		uint32_t m_Count;			// 运行次数
		
		bool m_IsRunning;			// 是不是正在运行
		COrderedThreadPool* m_Pool;			//线程池的指针

		CThreadInfo() :m_StartTime(0), m_Count(0), m_IsRunning(false) {}
	};



	//////////////////////////////////////////////////////////////////////////
	///>> 线程池,(采用被动唤醒方式)
	///>> 多线程多用户   用户之间无序执行, 用户任务顺序执行，
	class COrderedThreadPool
	{
	public:
		//************************************
		// 备注:      
		// 函数名:    ThreadPool
		// 函数全名:  HX_Thread::ThreadPool::ThreadPool
		// 访问权限:  public 
		// 返回值:    无
		// 说明:      构造函数
		// 参数: 	  uint32_t dwNum  线程池规模
		//************************************
		COrderedThreadPool(uint32_t dwNum = 2);

		//************************************
		// 备注:      
		// 函数名:    ~ThreadPool
		// 函数全名:  HX_Thread::ThreadPool::~ThreadPool
		// 访问权限:  virtual public 
		// 返回值:    
		// 说明:      析构函数
		//************************************
		virtual ~COrderedThreadPool();

		//************************************
		// 备注:      
		// 函数名:    addTheadTask
		// 函数全名:  HX_Thread::ThreadPool::addTheadTask
		// 访问权限:  public 
		// 返回值:    bool
		// 说明:     添加任务到任务队列
		// 参数: 	  int nSessionID   用户ID
		// 参数: 	  WS_THREAD_FUNCTION pFun   线程函数
		// 参数: 	  void * pObj   函数参数
		//************************************
		bool addTheadTask(int nSessionID, WS_THREAD_FUNCTION pFun, void* pObj);

		//************************************
		// 备注:      
		// 函数名:    updateThreadNum
		// 函数全名:  HX_Thread::ThreadPool::updateThreadNum
		// 访问权限:  public 
		// 返回值:    uint32_t
		// 说明:     调整线程池规模
		// 参数: 	  int32_t aNum   新的线程池大小
		//************************************
		uint32_t updateThreadNum(int32_t aNum);


	public:
		//************************************
		// 备注:      
		// 函数名:    EndAndWait
		// 函数全名:  HX_Thread::ThreadPool::EndAndWait
		// 访问权限:  public 
		// 返回值:    bool
		// 说明:     结束线程池, 并同步等待
		//************************************
		bool EndAndWait();

		//************************************
		// 备注:      
		// 函数名:    End
		// 函数全名:  HX_Thread::ThreadPool::End
		// 访问权限:  public 
		// 返回值:    bool
		// 说明:     结束线程池
		//************************************
		bool End();  

		//************************************
		// 备注:      
		// 函数名:    GetThreadNum
		// 函数全名:  HX_Thread::ThreadPool::GetThreadNum
		// 访问权限:  public 
		// 返回值:    uint32_t
		// 说明:     线程总数
		//************************************
		uint32_t GetThreadNum(); 

		//************************************
		// 备注:      
		// 函数名:    GetRunningThreadNum
		// 函数全名:  HX_Thread::ThreadPool::GetRunningThreadNum
		// 访问权限:  public 
		// 返回值:    uint32_t
		// 说明:     运行中的线程数量
		//************************************
		uint32_t GetRunningThreadNum(); 

		//************************************
		// 备注:      
		// 函数名:    IsRunning
		// 函数全名:  HX_Thread::ThreadPool::IsRunning
		// 访问权限:  public 
		// 返回值:    bool
		// 说明:     判断是不是运行中
		//************************************
		bool IsRunning(); 

		//************************************
		// 备注:      
		// 函数名:    GetTaskNum
		// 函数全名:  HX_Thread::ThreadPool::GetTaskNum
		// 访问权限:  public 
		// 返回值:    uint32_t
		// 说明:     获取指定用户阻塞的任务个数
		// 参数: 	  int nSessionID   用户ID
		//************************************
		uint32_t GetTaskNum(int nSessionID); 


	public:
		std::map<int, std::queue<CThreadTask*>> m_TaskMap;//队列
		std::vector<CThreadInfo*> m_ThreadVector; // 线程队列,谁被激活就谁处理

		CommonTools::mutex  m_mxThreadLock;// 线程队列锁
		CommonTools::mutex  m_mxTaskLock;// 线程队列锁

		uint32_t m_lThreadNum; //总线程数
		uint32_t m_lRunningNum; // 当前正在运行的线程数
		HANDLE  m_SemaphoreCall; 
		HANDLE  m_SemaphoreDel; // 通知删除线程信号量
		HANDLE  m_EventStopThread; // 通知所有线程关闭(强制关闭)
		HANDLE  m_EventWaitStopThread; // 通知所有线程关闭(等待完成所有任务在关闭)
		HANDLE  m_EventPool; // 关闭线程池用
	};

}



#endif


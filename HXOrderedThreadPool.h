/*!
* \brief �����̳߳�  ���û����߳�
*
* TODO: 
*
* \note �����̳߳�
*
* \author TIME
*
* \version 1.0
*
* \date ���� 2018
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
#define WS_THREAD_POOL_TASK_MAX (9999999) // ͬʱ������ڵ�������Ϊ9999999��
	typedef void (*WS_THREAD_FUNCTION)(void*);
	typedef unsigned int(*WS_THREAD_FUN)(void*);


	//////////////////////////////////////////////////////////////////////////
	///>> Task(����)
	class CThreadTask
	{
		enum {
			E_EMPTY,		// ������
			E_READY_RUN,	// �ȴ�ִ��
			E_RUNING,		// ִ����
			E_STOP			// ִ�н���
		};
	public:
		CThreadTask();
		CThreadTask(HX_Thread::WS_THREAD_FUNCTION afun);
		CThreadTask(HX_Thread::WS_THREAD_FUNCTION afun, void *pPara);
		
		//************************************
		// ��ע:      
		// ������:    ReadyRun
		// ����ȫ��:  HX_Thread::CThreadTask::ReadyRun
		// ����Ȩ��:  public 
		// ����ֵ:    void
		// ˵��:     ����ȴ���ִ��
		//************************************
		void ReadyRun();

		//************************************
		// ��ע:      
		// ������:    Run
		// ����ȫ��:  HX_Thread::CThreadTask::Run
		// ����Ȩ��:  public 
		// ����ֵ:    void
		// ˵��:     ����ִ����
		//************************************
		void Run();

		//************************************
		// ��ע:      
		// ������:    Stop
		// ����ȫ��:  HX_Thread::CThreadTask::Stop
		// ����Ȩ��:  public 
		// ����ֵ:    void
		// ˵��:     ����ִ�н���
		//************************************
		void Stop();

		//************************************
		// ��ע:      
		// ������:    Clear
		// ����ȫ��:  HX_Thread::CThreadTask::Clear
		// ����Ȩ��:  public 
		// ����ֵ:    void
		// ˵��:     �������
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
		unsigned char		m_byStatus;		// ��ǰ�߳�״̬
		WS_THREAD_FUNCTION	m_fun;			// ����
		void*				m_para;			// ����
	};

	//////////////////////////////////////////////////////////////////////////
	///>>�߳�
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
	///>> �߳���Ϣ
	class CThreadInfo
	{
	public:
		
		CThread m_ThreadObj;			// �̶߳���
		uint64_t m_StartTime;		// ���һ�����п�ʼʱ��
		uint32_t m_Count;			// ���д���
		
		bool m_IsRunning;			// �ǲ�����������
		COrderedThreadPool* m_Pool;			//�̳߳ص�ָ��

		CThreadInfo() :m_StartTime(0), m_Count(0), m_IsRunning(false) {}
	};



	//////////////////////////////////////////////////////////////////////////
	///>> �̳߳�,(���ñ������ѷ�ʽ)
	///>> ���̶߳��û�   �û�֮������ִ��, �û�����˳��ִ�У�
	class COrderedThreadPool
	{
	public:
		//************************************
		// ��ע:      
		// ������:    ThreadPool
		// ����ȫ��:  HX_Thread::ThreadPool::ThreadPool
		// ����Ȩ��:  public 
		// ����ֵ:    ��
		// ˵��:      ���캯��
		// ����: 	  uint32_t dwNum  �̳߳ع�ģ
		//************************************
		COrderedThreadPool(uint32_t dwNum = 2);

		//************************************
		// ��ע:      
		// ������:    ~ThreadPool
		// ����ȫ��:  HX_Thread::ThreadPool::~ThreadPool
		// ����Ȩ��:  virtual public 
		// ����ֵ:    
		// ˵��:      ��������
		//************************************
		virtual ~COrderedThreadPool();

		//************************************
		// ��ע:      
		// ������:    addTheadTask
		// ����ȫ��:  HX_Thread::ThreadPool::addTheadTask
		// ����Ȩ��:  public 
		// ����ֵ:    bool
		// ˵��:     ��������������
		// ����: 	  int nSessionID   �û�ID
		// ����: 	  WS_THREAD_FUNCTION pFun   �̺߳���
		// ����: 	  void * pObj   ��������
		//************************************
		bool addTheadTask(int nSessionID, WS_THREAD_FUNCTION pFun, void* pObj);

		//************************************
		// ��ע:      
		// ������:    updateThreadNum
		// ����ȫ��:  HX_Thread::ThreadPool::updateThreadNum
		// ����Ȩ��:  public 
		// ����ֵ:    uint32_t
		// ˵��:     �����̳߳ع�ģ
		// ����: 	  int32_t aNum   �µ��̳߳ش�С
		//************************************
		uint32_t updateThreadNum(int32_t aNum);


	public:
		//************************************
		// ��ע:      
		// ������:    EndAndWait
		// ����ȫ��:  HX_Thread::ThreadPool::EndAndWait
		// ����Ȩ��:  public 
		// ����ֵ:    bool
		// ˵��:     �����̳߳�, ��ͬ���ȴ�
		//************************************
		bool EndAndWait();

		//************************************
		// ��ע:      
		// ������:    End
		// ����ȫ��:  HX_Thread::ThreadPool::End
		// ����Ȩ��:  public 
		// ����ֵ:    bool
		// ˵��:     �����̳߳�
		//************************************
		bool End();  

		//************************************
		// ��ע:      
		// ������:    GetThreadNum
		// ����ȫ��:  HX_Thread::ThreadPool::GetThreadNum
		// ����Ȩ��:  public 
		// ����ֵ:    uint32_t
		// ˵��:     �߳�����
		//************************************
		uint32_t GetThreadNum(); 

		//************************************
		// ��ע:      
		// ������:    GetRunningThreadNum
		// ����ȫ��:  HX_Thread::ThreadPool::GetRunningThreadNum
		// ����Ȩ��:  public 
		// ����ֵ:    uint32_t
		// ˵��:     �����е��߳�����
		//************************************
		uint32_t GetRunningThreadNum(); 

		//************************************
		// ��ע:      
		// ������:    IsRunning
		// ����ȫ��:  HX_Thread::ThreadPool::IsRunning
		// ����Ȩ��:  public 
		// ����ֵ:    bool
		// ˵��:     �ж��ǲ���������
		//************************************
		bool IsRunning(); 

		//************************************
		// ��ע:      
		// ������:    GetTaskNum
		// ����ȫ��:  HX_Thread::ThreadPool::GetTaskNum
		// ����Ȩ��:  public 
		// ����ֵ:    uint32_t
		// ˵��:     ��ȡָ���û��������������
		// ����: 	  int nSessionID   �û�ID
		//************************************
		uint32_t GetTaskNum(int nSessionID); 


	public:
		std::map<int, std::queue<CThreadTask*>> m_TaskMap;//����
		std::vector<CThreadInfo*> m_ThreadVector; // �̶߳���,˭�������˭����

		CommonTools::mutex  m_mxThreadLock;// �̶߳�����
		CommonTools::mutex  m_mxTaskLock;// �̶߳�����

		uint32_t m_lThreadNum; //���߳���
		uint32_t m_lRunningNum; // ��ǰ�������е��߳���
		HANDLE  m_SemaphoreCall; 
		HANDLE  m_SemaphoreDel; // ֪ͨɾ���߳��ź���
		HANDLE  m_EventStopThread; // ֪ͨ�����̹߳ر�(ǿ�ƹر�)
		HANDLE  m_EventWaitStopThread; // ֪ͨ�����̹߳ر�(�ȴ�������������ڹر�)
		HANDLE  m_EventPool; // �ر��̳߳���
	};

}



#endif


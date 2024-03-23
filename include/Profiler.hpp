#pragma once
#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <thread>
#include <utility>

// @todo into repo

namespace profiler
{
#define PROF_ASSERT(cond, message)                                             \
    if (!(cond))                                                               \
    {                                                                          \
        std::cerr << (std::string{ "Assertion failed! " } + (message));        \
    }

template <class T>
class Singleton
{
public:
    static T& GetInstance()
    {
        if (!Singleton::s_Instance)
        {
            Singleton::s_Instance = createInstance();
        }
        return *Singleton::s_Instance;
    }

protected:
    explicit Singleton()
    {
        PROF_ASSERT(!Singleton::s_Instance,
                    "Singleton instance already exists");
        Singleton::s_Instance = static_cast<T*>(this);
    }

    ~Singleton()
    {
        Singleton::s_Instance = 0;
    }

    static T* createInstance()
    {
        return new T();
    }

private:
    static T* s_Instance;

    Singleton(const Singleton&) = default;
    auto operator=(const Singleton&) -> Singleton& = default;
};

template <class T>
T* Singleton<T>::s_Instance = 0;

struct ProfileResult
{
    std::string Name;
    std::chrono::duration<double, std::micro> Start;
    std::chrono::microseconds Elapsed;
    std::thread::id ThreadId;

    ProfileResult(std::string Name,
                  const std::chrono::duration<double, std::micro>& Start,
                  const std::chrono::microseconds& Elapsed,
                  const std::thread::id& ThreadId)
        : Name(std::move(Name)),
          Start(Start),
          Elapsed(Elapsed),
          ThreadId(ThreadId)
    {
    }
};

class ProfilerSession
{
public:
    explicit ProfilerSession(std::string name)
        : Name(std::move(name))
    {
    }

    std::string Name;
};

class Profiler : public Singleton<Profiler>
{
public:
    Profiler()
        : m_CurrentSession{ nullptr }
    {
    }
    ~Profiler()
    {
        if (m_CurrentSession)
        {
            EndSession();
        }
    }

    void BeginSession(const std::string& name,
                      const std::string& filePath = "result.json")
    {
        std::scoped_lock lock{ m_Mutex };
        if (m_CurrentSession)
        {

            PROF_ASSERT(false, "Profiling session already opened: {0}!" +
                                   m_CurrentSession->Name);
            EndSessionNoLock_();
        }
        m_OutStream.open(filePath);

        PROF_ASSERT(m_OutStream.is_open(),
                    "Could not open file: {0}!" + filePath);
        m_CurrentSession = new ProfilerSession{ name };
        WriteHeader();
    }
    void WriteProfile(const ProfileResult& result)
    {
        std::stringstream json{};

        json << std::setprecision(3) << std::fixed;
        json << ",{";
        json << R"("cat":"function",)";
        json << "\"dur\":" << (result.Elapsed.count()) << ',';
        json << R"("name":")" << result.Name << "\",";
        json << R"("ph":"X",)";
        json << "\"pid\":0,";
        json << "\"tid\":" << result.ThreadId << ",";
        json << "\"ts\":" << result.Start.count();
        json << "}";

        std::scoped_lock lock{ m_Mutex };

        PROF_ASSERT(m_CurrentSession, "No opened profiling session!");
        m_OutStream << json.str();
        m_OutStream.flush();
    }
    void EndSession()
    {
        std::scoped_lock lock{ m_Mutex };
        EndSessionNoLock_();
    }

    Profiler(const Profiler&) = delete;
    Profiler(Profiler&&) = delete;

private:
    void WriteHeader()
    {
        m_OutStream << R"({"otherData": {},"traceEvents":[{})";
        m_OutStream.flush();
    }
    void WriteFooter()
    {
        m_OutStream << "]}";
        m_OutStream.flush();
    }

private:
    void EndSessionNoLock_()
    {
        if (m_CurrentSession)
        {
            WriteFooter();
            m_OutStream.close();
            delete m_CurrentSession;
            m_CurrentSession = nullptr;
        }
    }

    std::mutex m_Mutex;
    std::ofstream m_OutStream;
    ProfilerSession* m_CurrentSession;
};

class ProfilerScope
{
public:
    explicit ProfilerScope(const std::string& name)
        : m_Name{ name }
    {
        m_Start = std::chrono::steady_clock::now();
    }
    ~ProfilerScope()
    {
        Stop();
    }

private:
    void Stop()
    {
        auto endTime{ std::chrono::steady_clock::now() };
        auto highResStart{ std::chrono::duration<double, std::micro>{
            m_Start.time_since_epoch() } };
        auto elapsed{
            std::chrono::time_point_cast<std::chrono::microseconds>(endTime)
                .time_since_epoch() -
            std::chrono::time_point_cast<std::chrono::microseconds>(m_Start)
                .time_since_epoch()
        };

        Profiler::GetInstance().WriteProfile(
            { m_Name, highResStart, elapsed, std::this_thread::get_id() });
    }

private:
    std::string m_Name;
    std::chrono::time_point<std::chrono::steady_clock> m_Start;
};

#ifdef PROFILER_ENABLE
#define PROFILER_BEGIN_SESSION(name, filename)                                 \
    ::profiler::Profiler::GetInstance().BeginSession((name), (filename));
#define PROFILER_END_SESSION() ::profiler::Profiler::GetInstance().EndSession();
#define PROFILER_SCOPE(name) PROFILER_SCOPE_LINE(name, __LINE__)
#define PROFILER_SCOPE_LINE(name, line) PROFILER_SCOPE_LINE2(name, line)
#define PROFILER_SCOPE_LINE2(name, line)                                       \
    ::profiler::ProfilerScope scope##line{ (name) };
#else
#define PROFILER_BEGIN_SESSION(name, filename)
#define PROFILER_END_SESSION()
#define PROFILER_SCOPE(name)
#endif // PROFILER_ENABLE

} // namespace profiler

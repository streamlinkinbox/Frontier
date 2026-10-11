#include "Engine/DeviceExchange/DriverProgress.h"
#include <cassert>
#include <stdexcept>

class Capture : public std::streambuf {
    std::mutex mutex;
    std::condition_variable changed;
    std::string text;
    std::streamsize xsputn(const char* data, std::streamsize count) override {
        {std::lock_guard lock(mutex);text.append(data,static_cast<size_t>(count));}
        changed.notify_all();return count;
    }
    int_type overflow(int_type c) override {
        if(!traits_type::eq_int_type(c,traits_type::eof())){char ch=traits_type::to_char_type(c);xsputn(&ch,1);}
        return traits_type::not_eof(c);
    }
public:
    bool WaitFor(const std::string& word){
        std::unique_lock lock(mutex);
        return changed.wait_for(lock,std::chrono::seconds(3),[&]{return text.find(word)!=std::string::npos;});
    }
    std::string Text(){std::lock_guard lock(mutex);return text;}
};
int main(){
    using Frontier::DriverProgress;
    using namespace std::chrono_literals;
    Capture capture;auto old=std::cerr.rdbuf(&capture);
    auto caller=std::this_thread::get_id();
    auto start=std::chrono::steady_clock::now();
    assert(DriverProgress::Call("instant",[&]{assert(std::this_thread::get_id()==caller);return 0;})==0);
    assert(std::chrono::steady_clock::now()-start<2s); // destructor wakes immediately, not after the 5s timer
    int Calls=0;
    assert(DriverProgress::Call("simulated pipeline",[&]{
        ++Calls;
        assert(std::this_thread::get_id()==caller);
        assert(capture.WaitFor("WAIT"));return 0;
    },10ms)==0);
    assert(Calls==1); // WAIT is an observer, not a pipeline retry loop
    assert(DriverProgress::Call("failure",[]{return -7;},10ms)==-7);
    bool caught=false;
    try{DriverProgress::Call("exception",[]()->int{throw std::runtime_error("test");},10ms);}
    catch(const std::runtime_error&){caught=true;}
    assert(caught);
    auto text=capture.Text();
    assert(text.find("BEGIN")!=std::string::npos);
    assert(text.find("DONE")!=std::string::npos);
    assert(text.find("VkResult=-7")!=std::string::npos);
    assert(text.find("percent/ETA unavailable")!=std::string::npos);
    assert(text.find("private_commit=")!=std::string::npos);
    assert(text.find("pid=")!=std::string::npos);
    assert(text.find("WAIT")<text.find("DONE",text.find("WAIT")));
    // Destructed scopes must not leave a background reporter using destroyed state.
    auto bytes=text.size();std::this_thread::sleep_for(30ms);assert(capture.Text().size()==bytes);
    std::cerr.rdbuf(old);
    std::cout<<text<<"PASS: begin/heartbeat/result, caller-thread execution, failure propagation, immediate stop, exception cleanup, memory fields, no orphan reporter. CPU simulation only.\n";
}

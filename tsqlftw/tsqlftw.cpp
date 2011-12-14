#pragma comment(lib, "node")

#include <node.h>
#include <v8.h>
#include <string>

#include "tsqlftwHelper.h"

using namespace node;
using namespace v8;

class tsqlftwObject: ObjectWrap
{
private:
    tsqlftwHelper* _tsqlftwHelper;
public:

    static Persistent<FunctionTemplate> s_ct;
    static void NODE_EXTERN Init(Handle<Object> target)
    {
        HandleScope scope;

        // set the constructor function
        Local<FunctionTemplate> t = FunctionTemplate::New(New);

        // set the node.js/v8 class name
        s_ct = Persistent<FunctionTemplate>::New(t);
        s_ct->InstanceTemplate()->SetInternalFieldCount(1);
        s_ct->SetClassName(String::NewSymbol("tsqlftwObject"));

        // registers a class member functions 
        NODE_SET_PROTOTYPE_METHOD(s_ct, "async", Async);
        NODE_SET_PROTOTYPE_METHOD(s_ct, "Connect", Connect);
		NODE_SET_PROTOTYPE_METHOD(s_ct, "Query", Query);

        target->Set(String::NewSymbol("tsqlftwObject"),
            s_ct->GetFunction());
    }

    tsqlftwObject() 
    {
        _tsqlftwHelper = tsqlftwHelper::New();
    }

    ~tsqlftwObject()
    {
        delete _tsqlftwHelper;
    }

    static Handle<Value> New(const Arguments& args)
    {
        HandleScope scope;
        tsqlftwObject* pm = new tsqlftwObject();
        pm->Wrap(args.This());
        return args.This();
    }

    struct Baton {
        uv_work_t request;
        tsqlftwHelper* tsqlftwHelper;
        Persistent<Function> callback;
        // A parameter that will be passed to the .Net library
		std::string query;
        // Tracking errors that happened in the worker function. You can use any
        // variables you want. E.g. in some cases, it might be useful to report
        // an error number.
        bool error;
        std::string error_message;

        // Custom data
        std::string result;
    };

    static bool Connect(std::string connString)
    {
        l<bool> result = Integer::New(so->_tsqlftwHelper->Connect());
        return scope.Close(result);
    }

    static Handle<Value> Query(const Arguments& args)
    {
        HandleScope scope;

        if (!args[0]->IsString()) {
            return ThrowException(Exception::TypeError(
                String::New("First argument must be a string")));
        }

        if (!args[1]->IsFunction()) {
            return ThrowException(Exception::TypeError(
                String::New("Second argument must be a callback function")));
        }

        Local<String> query = Local<String>::Cast(args[0]);
        // There's no ToFunction(), use a Cast instead.
        Local<Function> callback = Local<Function>::Cast(args[1]);

        tsqlftwObject* so = ObjectWrap::Unwrap<tsqlftwObject>(args.This());

        // create a state object
        Baton* baton = new Baton();
        baton->request.data = baton;
        baton->tsqlftwHelper = so->_tsqlftwHelper;
        baton->callback = Persistent<Function>::New(callback);
        baton->query = *v8::String::AsciiValue(query);

        // register a worker thread request
        uv_queue_work(uv_default_loop(), &baton->request,
            StartAsync, AfterAsync);

        return Undefined();
    }

    // this runs on the worker thread and should not callback or interact with node/v8 in any way
    static void StartAsync(uv_work_t* req)
    {
        Baton *baton = static_cast<Baton*>(req->data);
        baton->error = baton->tsqlftwHelper->Query(baton->query, baton->error_message, baton->result);
    }

    // this runs on the main thread and can call back into the JavaScript
    static void AfterAsync(uv_work_t *req)
    {
        HandleScope scope;
        Baton* baton = static_cast<Baton*>(req->data);

        if (baton->error) 
        {
            Local<Value> err = Exception::Error(
                String::New(baton->error_message.c_str()));
            Local<Value> argv[] = { err };

            TryCatch try_catch;
            baton->callback->Call(
                Context::GetCurrent()->Global(), 1, argv);

            if (try_catch.HasCaught()) {
                node::FatalException(try_catch);
            }        
        } 
        else 
        {
            const unsigned argc = 2;
            Local<Value> argv[argc] = {
                Local<Value>::New(Null()),
                Local<Value>::New(String::New(baton->result.c_str()))
            };

            TryCatch try_catch;
            baton->callback->Call(Context::GetCurrent()->Global(), argc, argv);

            if (try_catch.HasCaught()) {
                FatalException(try_catch);
            }
        }

        baton->callback.Dispose();
        delete baton;
    }
};

Persistent<FunctionTemplate> tsqlftwObject::s_ct;

extern "C" {
    void NODE_EXTERN init (Handle<Object> target)
    {
        tsqlftwObject::Init(target);
        LoadAssembly();
    }
    NODE_MODULE(tsqlftw, init);
}
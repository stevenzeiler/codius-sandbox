#include "codius-util.h"

#include <v8.h>
#include <node/node.h>
#include <vector>
#include <memory>
#include <unistd.h>

#include "debug.h"

using namespace v8;

struct CallbackData {
  Persistent<Function> callback;
  Persistent<Object> nodeThis;
  unsigned int id;
};

static uv_poll_t poll;

static std::vector<std::unique_ptr<CallbackData> > callbacks;

static void
cb_read_codius_result (uv_poll_t* req, int status, int events)
{
  Debug() << "got IPC result";
  std::unique_ptr<CallbackData> data;
  std::unique_ptr<codius_result_t> res (codius_read_result (3));

  Debug() << "responding to ID" << res->_id;

  if (res) {
    for (auto i = callbacks.begin(); i != callbacks.end(); i++) {
      if ((*i)->id == res->_id) {
        data = std::move(*i);
        callbacks.erase (i);
        Debug() << "found" << data->id;
        break;
      }
    }
  }

  assert (data);

  Handle<Value> argv[2] = {
    Undefined(),
    Undefined()
  };

  Debug() << "Calling";
  data->callback->Call (data->nodeThis, 2, argv);
  //data->nodeThis.Dispose();
  //data->callback.Dispose();
  if (callbacks.size() == 0) {
    uv_poll_stop (&poll);
  }
}

Handle<Value>
send_request(const Arguments& args)
{
  HandleScope scope;
  Handle<Value> callback;
  codius_request_t* req;
  std::unique_ptr<CallbackData> data;
  std::vector<char> api_name (1024);
  std::vector<char> method_name (1024);

  if (!args[0]->IsString()) {
    ThrowException(Exception::TypeError (String::New ("API name must be a string.")));
    return Undefined();
  }

  if (!args[1]->IsString()) {
    ThrowException (Exception::TypeError (String::New ("API method must be a string.")));
    return Undefined();
  }
  
  if (!args[args.Length()-1]->IsFunction()) {
    ThrowException (Exception::TypeError (String::New ("Callback must be last argument to call()")));
    return Undefined();
  }

  args[0]->ToString()->WriteUtf8 (api_name.data(), api_name.size());
  args[1]->ToString()->WriteUtf8 (method_name.data(), method_name.size());

  //FIXME: Build the json data request

  req = codius_request_new (api_name.data(), method_name.data());
  data = std::unique_ptr<CallbackData> (new CallbackData);
  data->nodeThis = Persistent<Object>::New (args.This());
  data->callback = Persistent<Function>::New (args[args.Length()-1].As<Function>());
  data->id = req->_id;
  Debug() << "Calling" << api_name.data() << "." << method_name.data() << "id" << data->id;
  callbacks.push_back (std::move (data));
  uv_poll_start (&poll, UV_READABLE, cb_read_codius_result);
  codius_write_request (3, req);
  return Undefined();
}

void
init(Handle<Object> exports)
{
  uv_loop_t* loop = uv_default_loop ();
  Local<FunctionTemplate> tpl = FunctionTemplate::New (send_request);
  exports->Set (String::NewSymbol ("call"), tpl->GetFunction());

  uv_poll_init_socket (loop, &poll, 3);
}

NODE_MODULE (codius_api, init);
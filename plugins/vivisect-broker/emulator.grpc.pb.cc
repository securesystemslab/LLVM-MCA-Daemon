// Generated by the gRPC C++ plugin.
// If you make any local change, they will be lost.
// source: emulator.proto

#include "emulator.pb.h"
#include "emulator.grpc.pb.h"

#include <functional>
#include <grpcpp/support/async_stream.h>
#include <grpcpp/support/async_unary_call.h>
#include <grpcpp/impl/channel_interface.h>
#include <grpcpp/impl/client_unary_call.h>
#include <grpcpp/support/client_callback.h>
#include <grpcpp/support/message_allocator.h>
#include <grpcpp/support/method_handler.h>
#include <grpcpp/impl/rpc_service_method.h>
#include <grpcpp/support/server_callback.h>
#include <grpcpp/impl/server_callback_handlers.h>
#include <grpcpp/server_context.h>
#include <grpcpp/impl/service_type.h>
#include <grpcpp/support/sync_stream.h>

static const char* Emulator_method_names[] = {
  "/Emulator/RecordEmulatorActions",
};

std::unique_ptr< Emulator::Stub> Emulator::NewStub(const std::shared_ptr< ::grpc::ChannelInterface>& channel, const ::grpc::StubOptions& options) {
  (void)options;
  std::unique_ptr< Emulator::Stub> stub(new Emulator::Stub(channel, options));
  return stub;
}

Emulator::Stub::Stub(const std::shared_ptr< ::grpc::ChannelInterface>& channel, const ::grpc::StubOptions& options)
  : channel_(channel), rpcmethod_RecordEmulatorActions_(Emulator_method_names[0], options.suffix_for_stats(),::grpc::internal::RpcMethod::NORMAL_RPC, channel)
  {}

::grpc::Status Emulator::Stub::RecordEmulatorActions(::grpc::ClientContext* context, const ::EmulatorActions& request, ::NextAction* response) {
  return ::grpc::internal::BlockingUnaryCall< ::EmulatorActions, ::NextAction, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), rpcmethod_RecordEmulatorActions_, context, request, response);
}

void Emulator::Stub::async::RecordEmulatorActions(::grpc::ClientContext* context, const ::EmulatorActions* request, ::NextAction* response, std::function<void(::grpc::Status)> f) {
  ::grpc::internal::CallbackUnaryCall< ::EmulatorActions, ::NextAction, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(stub_->channel_.get(), stub_->rpcmethod_RecordEmulatorActions_, context, request, response, std::move(f));
}

void Emulator::Stub::async::RecordEmulatorActions(::grpc::ClientContext* context, const ::EmulatorActions* request, ::NextAction* response, ::grpc::ClientUnaryReactor* reactor) {
  ::grpc::internal::ClientCallbackUnaryFactory::Create< ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(stub_->channel_.get(), stub_->rpcmethod_RecordEmulatorActions_, context, request, response, reactor);
}

::grpc::ClientAsyncResponseReader< ::NextAction>* Emulator::Stub::PrepareAsyncRecordEmulatorActionsRaw(::grpc::ClientContext* context, const ::EmulatorActions& request, ::grpc::CompletionQueue* cq) {
  return ::grpc::internal::ClientAsyncResponseReaderHelper::Create< ::NextAction, ::EmulatorActions, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(channel_.get(), cq, rpcmethod_RecordEmulatorActions_, context, request);
}

::grpc::ClientAsyncResponseReader< ::NextAction>* Emulator::Stub::AsyncRecordEmulatorActionsRaw(::grpc::ClientContext* context, const ::EmulatorActions& request, ::grpc::CompletionQueue* cq) {
  auto* result =
    this->PrepareAsyncRecordEmulatorActionsRaw(context, request, cq);
  result->StartCall();
  return result;
}

Emulator::Service::Service() {
  AddMethod(new ::grpc::internal::RpcServiceMethod(
      Emulator_method_names[0],
      ::grpc::internal::RpcMethod::NORMAL_RPC,
      new ::grpc::internal::RpcMethodHandler< Emulator::Service, ::EmulatorActions, ::NextAction, ::grpc::protobuf::MessageLite, ::grpc::protobuf::MessageLite>(
          [](Emulator::Service* service,
             ::grpc::ServerContext* ctx,
             const ::EmulatorActions* req,
             ::NextAction* resp) {
               return service->RecordEmulatorActions(ctx, req, resp);
             }, this)));
}

Emulator::Service::~Service() {
}

::grpc::Status Emulator::Service::RecordEmulatorActions(::grpc::ServerContext* context, const ::EmulatorActions* request, ::NextAction* response) {
  (void) context;
  (void) request;
  (void) response;
  return ::grpc::Status(::grpc::StatusCode::UNIMPLEMENTED, "");
}



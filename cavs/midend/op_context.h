#ifndef CAVS_MIDEND_OP_CONTEXT_H_
#define CAVS_MIDEND_OP_CONTEXT_H_

#include "cavs/midend/tensor.h"
#include "cavs/midend/graph_scheduler.h"
#include "cavs/proto/op_def.pb.h"
#include "cavs/util/stream_event_handle_pool.h"

#include <unordered_map>
#include <string>

namespace midend {

class OpContext {
 public:
  OpContext() : round_(0), gs_(NULL),
    stream_id_(-1), event_record_id_(-1), wait_for_event_id_(-1) {}
  inline const Tensor& Input(int idx) const;
  inline Tensor* Output(int idx);
  inline int InputSize() const;
  inline int OutputSize() const;
  inline void AppendInput(const Tensor* t);
  inline void AppendOutput(Tensor* t);
  inline OpContext* ExtractContext(const std::vector<int>& inp, const std::vector<int>& out);
  inline void SetStreamId(int id) { stream_id_ = id; }
  inline int GetStreamID() const { return stream_id_; }
  inline void SetWaitForEventId(int eid) { wait_for_event_id_ = eid; }
  inline void SetEventRecord(int eid) { event_record_id_ = eid; }

  //the followings are all about optimizations
  inline void SetRound(int r) { round_ = r; }
  inline int round() const { return round_; }
  inline void SetGraphScheduler(GraphSchedulerBase* gs) {
    CHECK(gs_ == NULL && gs);
    gs_ = gs;
  }
  inline GraphSchedulerBase* graph_scheduler() { return gs_; }
  inline static void SetDynDim(int dyn_dim) { dyn_dim_ = dyn_dim; }

  void SetTensorOffset();
  void ResetTensorOffset();
  void ScaleOutputTensor();
  void ScaleInputTensor();
  void SetZero();
  void WaitForEvent();
  void RecordMyEvent();

  std::string debug_info() const;
  static std::unordered_map<std::string, void*> repo_;

 private:
  inline static int dyn_dim() { return dyn_dim_; }
  std::vector<const Tensor*> inputs_;
  std::vector<Tensor*> outputs_;
  int stream_id_;
  int event_record_id_;
  int wait_for_event_id_;
  std::vector<int> inputs_event_ids_;
  int round_;
  GraphSchedulerBase* gs_;
  static int dyn_dim_;
};

inline const Tensor& OpContext::Input(int idx) const {
  CHECK(idx < inputs_.size())
    << idx << "\t" << inputs_.size();
  return *(inputs_.at(idx)); 
}

inline Tensor* OpContext::Output(int idx) { 
  CHECK(idx < outputs_.size());
  return outputs_.at(idx); 
}

inline int OpContext::InputSize() const {
  return inputs_.size();
}

inline int OpContext::OutputSize() const {
  return outputs_.size();
}

inline void OpContext::AppendInput(const Tensor* t) {
  inputs_.push_back(t);
}

inline void OpContext::AppendOutput(Tensor* t) {
  outputs_.push_back(t); 
}

inline OpContext* OpContext::ExtractContext(const std::vector<int>& inp, const std::vector<int>& out) {
  OpContext* ret = new OpContext();
  for (int i : inp) {
    CHECK(i < InputSize());
    ret->AppendInput(inputs_[i]);
  }
  for (int i : out) {
    CHECK(i < OutputSize());
    ret->AppendOutput(outputs_[i]);
  }
  return ret;
}

} //namespace midend
        
#endif

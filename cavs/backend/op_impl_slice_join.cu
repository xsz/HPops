#include "cavs/backend/op_impl.h"
#include "cavs/midend/tensor.h"
#include "cavs/util/macros_gpu.h"

using std::vector;
using std::string;
using ::midend::Tensor;

namespace backend {

template <typename T>
class SliceOpImpl : public OpImpl {
 public:
  explicit SliceOpImpl(const OpDef& def) :
    OpImpl(def), split_(-1), index_(-1), offset_(-1), stride_(-1) {
    //CHECK(GetSingleArg<bool>(op_def_, "ShareMemory"));
    if (GetSingleArg(def, "Split", 0) != 0) {
      split_ = GetSingleArg<int>(def, "Split"); 
      index_ = GetSingleArg<int>(def, "Index"); 
      CHECK(split_ > 0);
      CHECK(index_ >= 0);
    }else {
      offset_ = GetSingleArg<int>(def, "Offset");
      stride_ = GetSingleArg<int>(def, "Stride");
      CHECK(offset_ >= 0);
      CHECK(stride_ > 0);
    }
  }
  void Compute(OpContext* context) override;

 private:
  int offset_;
  int stride_;
  int split_;
  int index_;
};

template <typename T>
void SliceOpImpl<T>::Compute(OpContext* context) {
  const Tensor& x = context->Input(0);
  Tensor* y = context->Output(0);

  if (offset_ < 0) {
    CHECK(x.count()% split_ == 0) << x.count() << "\t" << split_;
    stride_ = x.count() / split_;
    offset_ = x.count() / split_ * index_;
  }
  CHECK(stride_ == y->count());

  checkCudaError(cudaMemcpy(y->mutable_data<T>(), x.data<T>()+offset_,
                            stride_*sizeof(T),
                            cudaMemcpyDeviceToDevice));
}

REGISTER_OP_IMPL_BUILDER(Key("Slice").Device("GPU"), SliceOpImpl<float>);

} //namespace backend
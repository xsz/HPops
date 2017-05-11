#include "cavs/midend/tensor.h"
#include "cavs/util/types.h"
#include "cavs/util/logging.h"
#include "cavs/util/macros_gpu.h"
#include "cavs/util/mpi_types.h"

#include <iomanip>

using std::string;
using std::vector;

namespace midend {

#define CASE(TYPE, STMTS)                             \
  case DataTypeToEnum<TYPE>::value: {                 \
    typedef TYPE T;                                   \
    STMTS;                                            \
    break;                                            \
  }

#define CASES(TYPE_ENUM, STMTS)                       \
  switch (TYPE_ENUM) {                                \
    CASE(float, STMTS)                                \
    CASE(double, STMTS)                               \
    CASE(int, STMTS)                                  \
    default:                                          \
      LOG(FATAL) << "Unsupported type:" << TYPE_ENUM; \
      break;                                          \
  }

template <typename T>
class TensorBuffer : public TensorBufferBase {
 public:
  TensorBuffer(Allocator* alloc, size_t elem) 
      : TensorBufferBase(alloc), elem_(elem) {
    data_ = alloc->Allocate<T>(elem_);   
  }
  ~TensorBuffer() override { alloc_->Deallocate<T>(data_); }
  FORCE_INLINE void* data() const override { return data_; }
  FORCE_INLINE size_t size() const override { return elem_*sizeof(T); }
  FORCE_INLINE size_t count() const override { return elem_; }

 private:
  T* data_;
  int elem_;

  DISALLOW_COPY_AND_ASSIGN(TensorBuffer);
};

string TensorShape::DebugInfo() const {
  string ret; 
  for (auto& s : shape_)
    ret += std::to_string(s) + ",";
  return ret;
}
string Tensor::DebugInfo() const {
  string ret; 
  ret += "\nname: " + name_;
  ret += "\nshape: " + shape_->DebugInfo();
  ret += "\ntype: " + std::to_string(type_);
  return ret;
}

template <>
void Tensor::DebugNumerical<float>() const {
  if (VLOG_IS_ON(V_EXHAUSTIVE_DEBUG)) {
    vector<float> res(count());
    if (device_type() == GPU) {
      checkCudaError(cudaMemcpy(res.data(), data<float>(),
            count()*sizeof(float), cudaMemcpyDeviceToHost));
    }else {
      checkCudaError(cudaMemcpy(res.data(), data<float>(),
            count()*sizeof(float), cudaMemcpyHostToHost));
    }
    VLOG(V_EXHAUSTIVE_DEBUG) << DebugInfo();
    float L2_norm = 0;
    for (int i = 0; i < count(); i++) {
      L2_norm += res[i]*res[i]; 
    }
    VLOG(V_EXHAUSTIVE_DEBUG) << name() << " L2 Norm: " << L2_norm;
    for (int i = 0; i < 20 && i < count(); i++)
      VLOG(V_EXHAUSTIVE_DEBUG) << name() << "[" << i << "]: "
                << std::setprecision(15) << res[i];
    for (int i = 0; i < count(); i++) {
      if (isnan(res[i])) {
        for (int j = i-100; j < i+100; j++) {
          if (name() == "global:Optimizer0:Variable2_grad")
             VLOG(V_EXHAUSTIVE_DEBUG) << name() << "[" << j << "]: "
                     << std::setprecision(15) << res[j];
        }
      }
      CHECK(!isnan(res[i])) << name() << ":\t" << i << "\t" << res[i];
    }
  }
}

Tensor::Tensor() : buf_(nullptr), shape_(nullptr) {}

Tensor::Tensor(const string& name, Allocator *a, 
        DataType type, const TensorShape& shape) 
    : name_(name), buf_(nullptr), shape_(nullptr), type_(type) {
  //shape_.reset(new TensorShape(shape));
  Rebase(a, type, std::move(shape));
}

Tensor::Tensor(const string& name, Allocator *a, 
        DataType type, TensorShape&& shape) 
    : name_(name), buf_(nullptr), shape_(nullptr), type_(type) {
  //shape_.reset(new TensorShape(std::move(shape)));
  Rebase(a, type, std::move(shape));
}

Tensor::Tensor(const std::string& name, const Tensor& t) {
  *this = t;
  name_ = name;
  //: buf_(t.buf_), shape_(t.shape_), type_(t.type_), name_(name) {
} 

Tensor& Tensor::operator =(const Tensor& t) {
  buf_   = t.buf_;
  shape_ = t.shape_;
  name_  = t.name_;
  type_  = t.type_;
  return *this;
}


void Tensor::Rebase(Allocator *a, 
        DataType type, const TensorShape& shape) {
  type_ = type;
  shape_.reset(new TensorShape(shape));
  CASES(type, buf_.reset(new TensorBuffer<T>(a, shape_->n_elements())));
}

void Tensor::Rebase(Allocator *a, 
        DataType type, TensorShape&& shape) {
  type_ = type;
  shape_.reset(new TensorShape(std::move(shape)));
  CASES(type, buf_.reset(new TensorBuffer<T>(a, shape_->n_elements())));
}

void Tensor::Rebase(Allocator *a, const Tensor& t) {
  Rebase(a, t.type_, *(t.shape_));
}

void Tensor::Reshape(const TensorShapeDef& shape) {
  int new_counts = 1;
  for (auto& dim : shape.dim())
    new_counts *= dim;
  CHECK(new_counts == count());
  shape_.reset(new TensorShape(shape));
}

void Tensor::Reshape(const vector<int>& dims) {
  int new_counts = 1;
  for (auto& dim : dims)
    new_counts *= dim;
  CHECK(new_counts == count());
  shape_.reset(new TensorShape(dims));
}

void Tensor::Reshape(const Tensor& t) {
  CHECK(t.count() == count());
  shape_ = t.shape_;
}

void Tensor::SyncWith(const Tensor& t) {
  //currently, syncwith function is used for
  //cross-device data synchronization
  CHECK(t.device_type() != device_type());
  //cudaMemcpyDefault can remove such a complexity
  //but for development, specified it clearly is better.
  if (t.device_type() == CPU && device_type() == GPU) {
    checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   t.buf_->size(), cudaMemcpyHostToDevice));
  }else if (t.device_type() == GPU && device_type() == CPU) {
    checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   t.buf_->size(), cudaMemcpyDeviceToHost));
  }
}

} //namespace midend

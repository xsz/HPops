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
  //FORCE_INLINE size_t count() const override { return elem_; }
  FORCE_INLINE void* Resize(int size) override { 
    CHECK(size % sizeof(T) == 0);
    CHECK(size != elem_*sizeof(T));
    if (data_) { alloc_->Deallocate<T>(data_); }
    data_ = alloc_->Allocate<T>(size/sizeof(T));   
  }

 private:
  T* data_;
  int elem_;

  DISALLOW_COPY_AND_ASSIGN(TensorBuffer);
};

string TensorShape::debug_info() const {
  string ret; 
  for (auto& s : shape_)
    ret += std::to_string(s) + ",";
  return ret;
}

string Tensor::debug_info() const {
  string ret; 
  ret += "\nname: " + name_;
  ret += "\nshape: " + shape_.debug_info();
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
    VLOG(V_EXHAUSTIVE_DEBUG) << debug_info();
    float L2_norm = 0;
    float checksum = 0;
    for (int i = 0; i < count(); i++) {
      L2_norm += res[i]*res[i]; 
      checksum += res[i];
    }
    VLOG(V_EXHAUSTIVE_DEBUG) << name()
      << "\tL2 Norm:\t" << L2_norm
      << "\t checksum:\t" << checksum;
    for (int i = 0; i < 20 && i < count(); i++)
      VLOG(V_EXHAUSTIVE_DEBUG) << name() << "[" << i << "]: "
                << std::setprecision(15) << res[i];
    for (int i = 0; i < count(); i++) {
      if (isnan(res[i]) || res[i] > 1.f) {
        //if (name() == "global:Optimizer0:Variable2_grad") {
             //VLOG(V_EXHAUSTIVE_DEBUG) << name() << "[" << i << "]: "
                     //<< std::setprecision(15) << res[i];
        //}
      }
      CHECK(!isnan(res[i])) << name() << ":\t" << i << "\t" << res[i];
    }
  }
}

Tensor::Tensor() :
  buf_(nullptr), name_(""),
  type_(DataType(0)), dynamic_(false) {}

Tensor::Tensor(const string& name, Allocator *a, 
        DataType type, const TensorShape& shape) 
    : buf_(nullptr), name_(name),
      type_(type), dynamic_(false) {
  CHECK(shape.dim() > 0);
  if (shape.dim(0) == -1) {
    dynamic_ = true; 
    shape_ = shape;
  }else {
    CHECK(shape.n_elements() > 0);
    Rebase(a, type, shape);
  }
}

Tensor::Tensor(const string& name, Allocator *a, 
        DataType type, TensorShape&& shape) 
    : buf_(nullptr), name_(name),
      type_(type), dynamic_(false) {
  CHECK(shape.dim() > 0);
  if (shape.dim(0) == -1) {
    dynamic_ = true; 
    shape_ = std::move(shape);
  }else {
    CHECK(shape.n_elements() > 0);
    Rebase(a, type, std::move(shape));
  }
}

Tensor::Tensor(const std::string& name, const Tensor& t) {
  *this = t;
  name_ = name;
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
  //shape_.reset(new TensorShape(shape));
  shape_ = shape;
  CASES(type, buf_.reset(new TensorBuffer<T>(a, shape_.n_elements())));
}

void Tensor::Rebase(Allocator *a, 
        DataType type, TensorShape&& shape) {
  type_ = type;
  //shape_.reset(new TensorShape(std::move(shape)));
  shape_ = std::move(shape);
  CASES(type, buf_.reset(new TensorBuffer<T>(a, shape_.n_elements())));
}

void Tensor::Rebase(Allocator *a, const Tensor& t) {
  Rebase(a, t.type_, t.shape_);
}

void Tensor::Reshape(const TensorShapeDef& shape) {
  CHECK(shape.dim_size() > 0);
  int new_counts = 1;
  for (auto& dim : shape.dim())
    new_counts *= dim;
  CHECK(new_counts == count() || shape.dim(0) == -1)
       << new_counts << "\tvs\t" << count();
  //shape_.reset(new TensorShape(shape));
  shape_ = TensorShape(shape);
}

void Tensor::Reshape(const vector<int>& dims) {
  CHECK(!dims.empty());
  int new_counts = 1;
  for (auto& dim : dims)
    new_counts *= dim;
  CHECK(new_counts == count() || dims[0] == -1)
       << new_counts << "\tvs\t" << count();
  //shape_.reset(new TensorShape(dims));
  shape_ = TensorShape(dims);
}

void Tensor::Reshape(const Tensor& t) {
  CHECK(t.count() == count());
  shape_ = t.shape_;
}

//the shape may be less than the real buffer size
void Tensor::Resize(const TensorShapeDef& shape) {
  int new_counts = 1;
  for (auto& dim : shape.dim())
    new_counts *= dim;
  if (new_counts > shape_.n_elements()) {
    buf_->Resize(new_counts);
  }
  shape_ = TensorShape(shape);
}


void Tensor::SyncWith(const Tensor& t) {
  //CHECK(t.device_type() != device_type());
  CHECK(t.buf_ && buf_);
  CHECK(t.shape_.n_elements() > 0 && shape_.n_elements() > 0);
  size_t size = count();
  CASES(type_, size*= sizeof(T));
  CHECK(size <= t.buf_->size());
  //cudaMemcpyDefault can remove such a complexity
  //but for development, specified it clearly is better.
  if (t.device_type() == CPU && device_type() == GPU) {
    //checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   //t.buf_->size(), cudaMemcpyHostToDevice));
    checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   size, cudaMemcpyHostToDevice));
  }else if (t.device_type() == GPU && device_type() == CPU) {
    checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   size, cudaMemcpyDeviceToHost));
  }else if (t.device_type() == CPU && device_type() == CPU) {
    checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   size, cudaMemcpyHostToHost));
  }else if (t.device_type() == GPU && device_type() == GPU) {
    checkCudaError(cudaMemcpy(buf_->data(), t.buf_->data(), 
                   size, cudaMemcpyDeviceToDevice));
  }else{
    LOG(FATAL) << "which device on earth?";
  }
}

} //namespace midend

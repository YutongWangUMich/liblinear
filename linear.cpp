#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <locale.h>
#include <ctime>
#include <iostream>
#include <vector>
#include "linear.h"
#include "newton.h"
int liblinear_version = LIBLINEAR_VERSION;
typedef signed char schar;
template <class T> static inline void my_swap(T& x, T& y) { T t=x; x=y; y=t; }
#ifndef min
template <class T> static inline T min(T x,T y) { return (x<y)?x:y; }
#endif
#ifndef max
template <class T> static inline T max(T x,T y) { return (x>y)?x:y; }
#endif
template <class S, class T> static inline void clone(T*& dst, S* src, int n)
{
	dst = new T[n];
	memcpy((void *)dst,(void *)src,sizeof(T)*n);
}



// This value is used to define epsilon in the Solver function
// See Shark/Test/Algorithms/Trainers/LinearSvmTrainer.cpp
// of the Shark-ML package.
#define MAX_KKT_VIOLATION 1e-5


#define STOPPING_CRITERION 0.0009

/* #define MEASURE_HEAP_SATURATION */
#define TRACE_OPTIM_TRAJ

#ifdef TRACE_OPTIM_TRAJ
struct stopwatch{
  std::clock_t    start_time;
  std::clock_t    paused_time;
  bool paused;
  stopwatch(){
    start_time = std::clock();
    paused = false;
  }

  void pause(){
    paused_time = std::clock();
    paused = true;
  }
  void resume(){
    if(paused){
      // advance the start time
      start_time += std::clock()-paused_time;
    }
  }

  double get_time(){
    return (std::clock() - start_time)/ (double)(CLOCKS_PER_SEC / 1000);
  }
};
#endif


/* #define DIAGNOSTIC2 */


#define INF HUGE_VAL
#define Malloc(type,n) (type *)malloc((n)*sizeof(type))

static void print_string_stdout(const char *s)
{
	fputs(s,stdout);
	fflush(stdout);
}
static void print_null(const char *s) {}

static void (*liblinear_print_string) (const char *) = &print_string_stdout;

#if 1
static void info(const char *fmt,...)
{
	char buf[BUFSIZ];
	va_list ap;
	va_start(ap,fmt);
	vsprintf(buf,fmt,ap);
	va_end(ap);
	(*liblinear_print_string)(buf);
}
#else
static void info(const char *fmt,...) {}
#endif
class sparse_operator
{
public:
	static double nrm2_sq(const feature_node *x)
	{
		double ret = 0;
		while(x->index != -1)
		{
			ret += x->value*x->value;
			x++;
		}
		return (ret);
	}

	static double dot(const double *s, const feature_node *x)
	{
		double ret = 0;
		while(x->index != -1)
		{
			ret += s[x->index-1]*x->value;
			x++;
		}
		return (ret);
	}

	static double sparse_dot(const feature_node *x1, const feature_node *x2)
	{
		double ret = 0;
		while(x1->index != -1 && x2->index != -1)
		{
			if(x1->index == x2->index)
			{
				ret += x1->value * x2->value;
				++x1;
				++x2;
			}
			else
			{
				if(x1->index > x2->index)
					++x2;
				else
					++x1;
			}
		}
		return (ret);
	}

	static void axpy(const double a, const feature_node *x, double *y)
	{
		while(x->index != -1)
		{
			y[x->index-1] += a*x->value;
			x++;
		}
	}
};

// L2-regularized empirical risk minimization
// min_w w^Tw/2 + \sum C_i \xi(w^Tx_i), where \xi() is the loss

class l2r_erm_fun: public function
{
public:
	l2r_erm_fun(const problem *prob, const parameter *param, double *C);
	~l2r_erm_fun();

	double fun(double *w);
	double linesearch_and_update(double *w, double *d, double *f, double *g, double alpha);
	int get_nr_variable(void);

protected:
	virtual double C_times_loss(int i, double wx_i) = 0;
	void Xv(double *v, double *Xv);
	void XTv(double *v, double *XTv);

	double *C;
	const problem *prob;
	double *wx;
	double *tmp; // a working array
	double wTw;
	int regularize_bias;
};

l2r_erm_fun::l2r_erm_fun(const problem *prob, const parameter *param, double *C)
{
	int l=prob->l;

	this->prob = prob;

	wx = new double[l];
	tmp = new double[l];
	this->C = C;
	this->regularize_bias = param->regularize_bias;
}

l2r_erm_fun::~l2r_erm_fun()
{
	delete[] wx;
	delete[] tmp;
}

double l2r_erm_fun::fun(double *w)
{
	int i;
	double f=0;
	int l=prob->l;
	int w_size=get_nr_variable();

	wTw = 0;
	Xv(w, wx);

	for(i=0;i<w_size;i++)
		wTw += w[i]*w[i];
	if(regularize_bias == 0)
		wTw -= w[w_size-1]*w[w_size-1];
	for(i=0;i<l;i++)
		f += C_times_loss(i, wx[i]);
	f = f + 0.5 * wTw;

	return(f);
}

int l2r_erm_fun::get_nr_variable(void)
{
	return prob->n;
}

// On entry *f must be the function value of w
// On exit w is updated and *f is the new function value
double l2r_erm_fun::linesearch_and_update(double *w, double *s, double *f, double *g, double alpha)
{
	int i;
	int l = prob->l;
	double sTs = 0;
	double wTs = 0;
	double gTs = 0;
	double eta = 0.01;
	int w_size = get_nr_variable();
	int max_num_linesearch = 20;
	double fold = *f;
	Xv(s, tmp);

	for (i=0;i<w_size;i++)
	{
		sTs += s[i] * s[i];
		wTs += s[i] * w[i];
		gTs += s[i] * g[i];
	}
	if(regularize_bias == 0)
	{
		// bias not used in calculating (w + \alpha s)^T (w + \alpha s)
		sTs -= s[w_size-1] * s[w_size-1];
		wTs -= s[w_size-1] * w[w_size-1];
	}

	int num_linesearch = 0;
	for(num_linesearch=0; num_linesearch < max_num_linesearch; num_linesearch++)
	{
		double loss = 0;
		for(i=0;i<l;i++)
		{
			double inner_product = tmp[i] * alpha + wx[i];
			loss += C_times_loss(i, inner_product);
		}
		*f = loss + (alpha * alpha * sTs + wTw) / 2.0 + alpha * wTs;
		if (*f - fold <= eta * alpha * gTs)
		{
			for (i=0;i<l;i++)
				wx[i] += alpha * tmp[i];
			break;
		}
		else
			alpha *= 0.5;
	}

	if (num_linesearch >= max_num_linesearch)
	{
		*f = fold;
		return 0;
	}
	else
		for (i=0;i<w_size;i++)
			w[i] += alpha * s[i];

	wTw += alpha * alpha * sTs + 2* alpha * wTs;
	return alpha;
}

void l2r_erm_fun::Xv(double *v, double *Xv)
{
	int i;
	int l=prob->l;
	feature_node **x=prob->x;

	for(i=0;i<l;i++)
		Xv[i]=sparse_operator::dot(v, x[i]);
}

void l2r_erm_fun::XTv(double *v, double *XTv)
{
	int i;
	int l=prob->l;
	int w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		XTv[i]=0;
	for(i=0;i<l;i++)
		sparse_operator::axpy(v[i], x[i], XTv);
}

class l2r_lr_fun: public l2r_erm_fun
{
public:
	l2r_lr_fun(const problem *prob, const parameter *param, double *C);
	~l2r_lr_fun();

	void grad(double *w, double *g);
	void Hv(double *s, double *Hs);

	void get_diag_preconditioner(double *M);

private:
	double *D;
	double C_times_loss(int i, double wx_i);
};

l2r_lr_fun::l2r_lr_fun(const problem *prob, const parameter *param, double *C):
	l2r_erm_fun(prob, param, C)
{
	int l=prob->l;
	D = new double[l];
}

l2r_lr_fun::~l2r_lr_fun()
{
	delete[] D;
}

double l2r_lr_fun::C_times_loss(int i, double wx_i)
{
	double ywx_i = wx_i * prob->y[i];
	if (ywx_i >= 0)
		return C[i]*log(1 + exp(-ywx_i));
	else
		return C[i]*(-ywx_i + log(1 + exp(ywx_i)));
}

void l2r_lr_fun::grad(double *w, double *g)
{
	int i;
	double *y=prob->y;
	int l=prob->l;
	int w_size=get_nr_variable();

	for(i=0;i<l;i++)
	{
		tmp[i] = 1/(1 + exp(-y[i]*wx[i]));
		D[i] = tmp[i]*(1-tmp[i]);
		tmp[i] = C[i]*(tmp[i]-1)*y[i];
	}
	XTv(tmp, g);

	for(i=0;i<w_size;i++)
		g[i] = w[i] + g[i];
	if(regularize_bias == 0)
		g[w_size-1] -= w[w_size-1];
}

void l2r_lr_fun::get_diag_preconditioner(double *M)
{
	int i;
	int l = prob->l;
	int w_size=get_nr_variable();
	feature_node **x = prob->x;

	for (i=0; i<w_size; i++)
		M[i] = 1;
	if(regularize_bias == 0)
		M[w_size-1] = 0;

	for (i=0; i<l; i++)
	{
		feature_node *xi = x[i];
		while (xi->index!=-1)
		{
			M[xi->index-1] += xi->value*xi->value*C[i]*D[i];
			xi++;
		}
	}
}

void l2r_lr_fun::Hv(double *s, double *Hs)
{
	int i;
	int l=prob->l;
	int w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		Hs[i] = 0;
	for(i=0;i<l;i++)
	{
		feature_node * const xi=x[i];
		double xTs = sparse_operator::dot(s, xi);

		xTs = C[i]*D[i]*xTs;

		sparse_operator::axpy(xTs, xi, Hs);
	}
	for(i=0;i<w_size;i++)
		Hs[i] = s[i] + Hs[i];
	if(regularize_bias == 0)
		Hs[w_size-1] -= s[w_size-1];
}

class l2r_l2_svc_fun: public l2r_erm_fun
{
public:
	l2r_l2_svc_fun(const problem *prob, const parameter *param, double *C);
	~l2r_l2_svc_fun();

	void grad(double *w, double *g);
	void Hv(double *s, double *Hs);

	void get_diag_preconditioner(double *M);

protected:
	void subXTv(double *v, double *XTv);

	int *I;
	int sizeI;

private:
	double C_times_loss(int i, double wx_i);
};

l2r_l2_svc_fun::l2r_l2_svc_fun(const problem *prob, const parameter *param, double *C):
	l2r_erm_fun(prob, param, C)
{
	I = new int[prob->l];
}

l2r_l2_svc_fun::~l2r_l2_svc_fun()
{
	delete[] I;
}

double l2r_l2_svc_fun::C_times_loss(int i, double wx_i)
{
	double d = 1 - prob->y[i] * wx_i;
	if (d > 0)
		return C[i] * d * d;
	else
		return 0;
}

void l2r_l2_svc_fun::grad(double *w, double *g)
{
	int i;
	double *y=prob->y;
	int l=prob->l;
	int w_size=get_nr_variable();

	sizeI = 0;
	for (i=0;i<l;i++)
	{
		tmp[i] = wx[i] * y[i];
		if (tmp[i] < 1)
		{
			tmp[sizeI] = C[i]*y[i]*(tmp[i]-1);
			I[sizeI] = i;
			sizeI++;
		}
	}
	subXTv(tmp, g);

	for(i=0;i<w_size;i++)
		g[i] = w[i] + 2*g[i];
	if(regularize_bias == 0)
		g[w_size-1] -= w[w_size-1];
}

void l2r_l2_svc_fun::get_diag_preconditioner(double *M)
{
	int i;
	int w_size=get_nr_variable();
	feature_node **x = prob->x;

	for (i=0; i<w_size; i++)
		M[i] = 1;
	if(regularize_bias == 0)
		M[w_size-1] = 0;

	for (i=0; i<sizeI; i++)
	{
		int idx = I[i];
		feature_node *xi = x[idx];
		while (xi->index!=-1)
		{
			M[xi->index-1] += xi->value*xi->value*C[idx]*2;
			xi++;
		}
	}
}

void l2r_l2_svc_fun::Hv(double *s, double *Hs)
{
	int i;
	int w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		Hs[i]=0;
	for(i=0;i<sizeI;i++)
	{
		feature_node * const xi=x[I[i]];
		double xTs = sparse_operator::dot(s, xi);

		xTs = C[I[i]]*xTs;

		sparse_operator::axpy(xTs, xi, Hs);
	}
	for(i=0;i<w_size;i++)
		Hs[i] = s[i] + 2*Hs[i];
	if(regularize_bias == 0)
		Hs[w_size-1] -= s[w_size-1];
}

void l2r_l2_svc_fun::subXTv(double *v, double *XTv)
{
	int i;
	int w_size=get_nr_variable();
	feature_node **x=prob->x;

	for(i=0;i<w_size;i++)
		XTv[i]=0;
	for(i=0;i<sizeI;i++)
		sparse_operator::axpy(v[i], x[I[i]], XTv);
}

class l2r_l2_svr_fun: public l2r_l2_svc_fun
{
public:
	l2r_l2_svr_fun(const problem *prob, const parameter *param, double *C);

	void grad(double *w, double *g);

private:
	double C_times_loss(int i, double wx_i);
	double p;
};

l2r_l2_svr_fun::l2r_l2_svr_fun(const problem *prob, const parameter *param, double *C):
	l2r_l2_svc_fun(prob, param, C)
{
	this->p = param->p;
	this->regularize_bias = param->regularize_bias;
}

double l2r_l2_svr_fun::C_times_loss(int i, double wx_i)
{
	double d = wx_i - prob->y[i];
	if(d < -p)
		return C[i]*(d+p)*(d+p);
	else if(d > p)
		return C[i]*(d-p)*(d-p);
	return 0;
}

void l2r_l2_svr_fun::grad(double *w, double *g)
{
	int i;
	double *y=prob->y;
	int l=prob->l;
	int w_size=get_nr_variable();
	double d;

	sizeI = 0;
	for(i=0;i<l;i++)
	{
		d = wx[i] - y[i];

		// generate index set I
		if(d < -p)
		{
			tmp[sizeI] = C[i]*(d+p);
			I[sizeI] = i;
			sizeI++;
		}
		else if(d > p)
		{
			tmp[sizeI] = C[i]*(d-p);
			I[sizeI] = i;
			sizeI++;
		}

	}
	subXTv(tmp, g);

	for(i=0;i<w_size;i++)
		g[i] = w[i] + 2*g[i];
	if(regularize_bias == 0)
		g[w_size-1] -= w[w_size-1];
}




// Calculate the primal objective 
std::pair<double,double> calc_WW_primal_obj(
    const problem *prob,
		int nr_class,
    double * w,
    double C
    ){

  double w_val;
  int w_size = prob->n;
  int l = prob->l;

  int idx, s;
  double norm_w = 0;
  for(idx = 0; idx < w_size; idx++){
    for(s=0;s<nr_class;s++){
      w_val = w[idx*nr_class+s];
      norm_w += (w_val*w_val);
    }
  }

  int i,yi;
  const feature_node *xi;
  double * wx = new double[nr_class];

  double WW_hinge_risk = 0;
  double val;
  for(i = 0; i < l; i++){
    yi = (int)prob->y[i];
    for(s=0;s<nr_class;s++){
      wx[s] = 0.0;
    }

    // compute the k dimension vector w'xi
    for(xi = prob->x[i]; (idx=xi->index)!=-1;xi++){
      for(s=0;s<nr_class;s++){
        wx[s] += w[(idx-1)*nr_class+s]*xi->value;
      }
    }

    for(s=0; s<nr_class; s++){
      if(s != yi){
        val = 1 - (wx[yi] - wx[s]);
        if(val > 0){
          WW_hinge_risk += val;
        }
      }
    }
  }
  printf("%f,%f,", norm_w/2, C*WW_hinge_risk);
  return std::pair<double,double>(norm_w/2, C*WW_hinge_risk);
  /* return (norm_w/2) + C*WW_hinge_risk; */
}


// A coordinate descent algorithm for
// multi-class support vector machines by Weston and Watkins using reflection code theory
//
// UPDATE THIS!
//  min_{\alpha}  0.5 \sum_m ||w_m(\alpha)||^2 + \sum_i \sum_m e^m_i alpha^m_i
//    s.t.     \alpha^m_i <= C^m_i \forall m,i , \sum_m \alpha^m_i=0 \forall i
//
//  where e^m_i = 0 if y_i  = m,
//        e^m_i = 1 if y_i != m,
//  C^m_i = C if m  = y_i,
//  C^m_i = 0 if m != y_i,
//  and w_m(\alpha) = \sum_i \alpha^m_i x_i
//
// Given:
// x, y, C
// eps is the stopping tolerance
//
// solution will be put in w
//

template <class T>
struct T_heap {
	int _size;

  struct _tuple{
    double val;
    T data;
  };

	_tuple * a;

	T_heap(int max_size)
	{
		_size = 0;
		a = new _tuple[max_size];
	}
	~T_heap()
	{
		delete [] a;
	}

	bool cmp(const _tuple & left, const _tuple & right)
	{
		return left.val < right.val;
	}

	int size()
	{
		return _size;
	}
	void push(double val, T data)
	{
		a[_size].val = val;
    a[_size].data = data;
		_size++;
		int i = _size-1;
		while(i)
		{
			int p = (i-1)/2;
			if(cmp(a[p], a[i]))
			{
				my_swap(a[i], a[p]);
				i = p;
			}
			else
				break;
		}
	}
	void pop()
	{
		_size--;
		a[0] = a[_size];
		int i = 0;
		while(i*2+1 < _size)
		{
			int l = i*2+1;
			int r = i*2+2;
			if(r < _size && cmp(a[l], a[r]))
				l = r;
			if(cmp(a[i], a[l]))
			{
				my_swap(a[i], a[l]);
				i = l;
			}
			else
				break;
		}
	}
	_tuple top()
	{
		return a[0];
	}

#ifdef DIAGNOSTIC2
  void print_state(){
    for(int i = 0; i < _size; i++){
      std::cout << "(" << a[i].val << "," << a[i].data << "), ";
    }
    std::cout << std::endl;
  }
#endif


};





class Solver_MCSVM_WW
{
  public:
		Solver_MCSVM_WW(const problem *prob, int nr_class, double C, double eps=0.1, int max_iter=5);
    ~Solver_MCSVM_WW();
    void Solve(double *w);
  private:


    /* double epsilon = 0.1 * MAX_KKT_VIOLATION; */
    double epsilon = 0;
		double solve_sub_problem(
      T_heap<bool> & UpDnHeap,
      T_heap<int> & IdxHeap,
      double * vSort,
      int * vIdx,
      double *beta);


		double C;
		int w_size, l; // l is the number of instances
		int nr_class;
		int max_iter;
		double eps;
		const problem *prob;

    /* double * vSort; */
    /* int * vIdx; */
};


Solver_MCSVM_WW::~Solver_MCSVM_WW()
{
}




Solver_MCSVM_WW::Solver_MCSVM_WW(const problem *prob, int nr_class, double C, double eps, int max_iter)
{
	this->w_size = prob->n;
	this->l = prob->l;
	this->nr_class = nr_class;
	this->eps = eps;
	this->max_iter = max_iter;
	this->prob = prob;
	this->C = C;
}



#ifdef DIAGNOSTIC2
template <class T>
void print_array(T * v, int length){
  printf("[");
  for(int i = 0; i< length -1; i++){
    printf("%f,", v[i]);
  }
  printf("%f]\n", v[length-1]);
}
#endif



double Solver_MCSVM_WW::solve_sub_problem(
    T_heap<bool> & UpDnHeap,
    T_heap<int> & IdxHeap,
    double * vSort,
    int * vIdx,
    double * beta){

  // modifies the following state variables
  //   double * vSort
  //   int * vIdx
  //
  // assumes that beta has been properly initialized with zeros

  // EXPLANATION
  // set * denote an arbitrary number strictly between 0 and C
  //
  //   num_up           num_mi          num_dn
  //      |                |               |
  //  /----------\   /-----------\   /----------\
  //  |          |   |           |   |          |
  // [C, C, ..., C,  *, *, ... , *,  0, 0, ..., 0]            Optimizer
  // 
  // [v1    .... v3  v4    ...   v6  v7    ...  v9]           vSort
  //             ^   ^           ^   ^
  //             |   |           |   |
  //             X   Y           U   V
  //
  // X = vId[num_up-1].val
  // Y = vId[num_up].val
  // U = vId[num_up+num_mi-1].val
  // V = vId[num_up+num_mi].val

  int num_up = 0, num_mi = 0, num_dn;
  double sum_v_mi = 0;
  bool up;
  double val;

  int nvIdx = 0, nvSort = 0, nvPos = IdxHeap.size();
  vSort[nvSort] = IdxHeap.top().val;
  nvSort++;

#ifdef MEASURE_HEAP_SATURATION
  int niter = 0;
#endif

  while(UpDnHeap.size()>0){
#ifdef MEASURE_HEAP_SATURATION
    niter++;
#endif

    up = UpDnHeap.top().data;
    val = UpDnHeap.top().val;

    UpDnHeap.pop();
    if(up){
      sum_v_mi -= (val+C);
      num_up++;
      num_mi--;
    }else{
      /* std::cout << "idx: " << IdxHeap.top().data << " at " << nvIdx << "\n"; */
      vIdx[nvIdx] = IdxHeap.top().data;
      IdxHeap.pop();

      if(nvSort < nvPos){
        vSort[nvSort] = IdxHeap.top().val;
      }

      sum_v_mi += val;
      num_mi++;

      nvIdx++;
      nvSort++;
    }

    double gamma = (C*num_up + sum_v_mi)/(num_mi+1.0);

    num_dn = nr_class - 1 - num_up - num_mi;


    bool kkt = 1;

    if(num_up > 0){
      kkt *= ((C+gamma) <= vSort[num_up-1]);
    }

    if(num_mi > 0){
      kkt *= (vSort[num_up] <= (C + gamma));
      kkt *= (gamma <= vSort[num_up+num_mi-1]);
    }

    if(num_dn > 0 && num_up+num_mi < nvPos){
      kkt *= (vSort[num_up+num_mi] <= gamma);
    }
    /* double beta_sum = C*num_up + sum_v_mi - gamma * num_mi; */

   if(kkt){
      double beta_val;
      for(int j = 0; j < nvIdx; j++){
        if(j < num_up){
          beta_val = C;
        }else{
          beta_val = vSort[j] - gamma;
          /* beta_sum += beta_val; */
        }
        beta[vIdx[j]] = beta_val;
      }
#ifdef MEASURE_HEAP_SATURATION
      std::cout << niter << std::endl;
#endif
      return C*num_up + sum_v_mi - gamma * num_mi;
    }
  }
  printf("did not find kkt");
  exit(0);
}


void Solver_MCSVM_WW::Solve(double *w){

#ifdef TRACE_OPTIM_TRAJ
  stopwatch SW;
  double dual_gap_init;
#endif

  int i,s,j;
  int idx;
	double *alpha =  new double[l*(nr_class-1)];
  double *alpha_block_sums = new double[l];

  
	double *alpha_new = new double[nr_class-1];
  double *del_alpha_pi = new double[nr_class];

  double *x_sq_norms = new double[l];
  double *wxi = new double[nr_class];


  double * vSort = new double[nr_class-1];
  int * vIdx = new int[nr_class-1];

  double * v_pos = new double[nr_class-1];
  int * v_pos_idx = new int[nr_class-1];
  int nv_pos;

  T_heap<bool> UpDnHeap(2*(nr_class-1));
  T_heap<int> IdxHeap(nr_class-1);

  const feature_node *xi;

  // Compute the squared norms of all the samples
  for(i=0;i<l;i++)
  {
    x_sq_norms[i] = 0;
    xi = prob->x[i];
    while(xi->index != -1)
    {
      double val = xi->value;
      x_sq_norms[i] += (val*val);
      xi++;
    }
  }
	int *index = new int[l];
	for(i=0;i<l;i++)
	{
		index[i] = i;
    alpha_block_sums[i] = 0;
	}


	// Initialize alpha
	for(i=0;i<l*(nr_class-1);i++)
		alpha[i] = 0;

	for(i=0;i<w_size*nr_class;i++)
		w[i] = 0;

  // outer loop
  int iter = 0;
  while(iter<max_iter){

    // shuffle indices
		for(i=0;i<l;i++)
		{
			j = i+rand()%(l-i);
			my_swap(index[i], index[j]);
		}

    // inner loop
    for(j=0;j<l;j++){
      i = index[j];
      int yi = (int)prob->y[i];
      

      // reset variables wxi and alpha_new
      for(s=0;s<nr_class-1;s++){
        wxi[s] = 0;
        alpha_new[s] = 0;
      }
      wxi[nr_class-1] = 0;

      // compute the k-1 dimension vector w'xi
      for(xi = prob->x[i]; (idx=xi->index)!=-1;xi++){
        for(s=0;s<nr_class;s++){
          wxi[s] += w[(idx-1)*nr_class+s]*xi->value;
        }
      }

      double * alpha_old = alpha + i*(nr_class-1);
      double sum_alpha = alpha_block_sums[i];

      double nsxi = x_sq_norms[i];

      if(yi > 0){
        my_swap(wxi[0],wxi[yi]);
      }

      nv_pos = 0;
      for(s=0;s<nr_class-1;s++){
        double val = (1 - (wxi[0]-wxi[s+1]))/nsxi + alpha_old[s] + sum_alpha;

        if(val > 0){
          v_pos[nv_pos] = val;
          v_pos_idx[nv_pos] = s;
          nv_pos++;
        }
      }

      UpDnHeap._size = 0;
      IdxHeap._size = 0;
      for(s = 0; s < nv_pos; s++){
        double val = v_pos[s];
        idx = v_pos_idx[s];
        IdxHeap.push(val, idx);
        UpDnHeap.push(val, 0);
        if(val - C > 0){
          UpDnHeap.push(val - C, 1);
        }
      }




      if(IdxHeap.size()>0){
/* #ifdef MEASURE_HEAP_SATURATION */
/*         std::cout<<IdxHeap.size() << ", "; */
/* #endif */
        alpha_block_sums[i] = solve_sub_problem(UpDnHeap, IdxHeap, vSort, vIdx, alpha_new);
      }else{
        alpha_block_sums[i] = 0;
      }


      del_alpha_pi[0] = 0;
      for(s=0;s<nr_class-1;s++){
        del_alpha_pi[s+1] = -(alpha_new[s]-alpha_old[s]);
        alpha_old[s] = alpha_new[s];
        del_alpha_pi[0] += -del_alpha_pi[s+1];
      }

      if(yi > 0){
        my_swap(del_alpha_pi[0],del_alpha_pi[yi]);
      }

      for(s=0;s<nr_class;s++){
        if(del_alpha_pi[s]==0) continue;
        for(xi = prob->x[i]; (idx=xi->index)!=-1;xi++){
          w[(idx-1)*nr_class+s] += (xi->value)*del_alpha_pi[s];
        }
      }

    }
#ifdef TRACE_OPTIM_TRAJ
    SW.pause();
    double elapsed_time = SW.get_time();
    std::cout << elapsed_time << ","; // TIME
    double sum_alpha = 0;
    for(i=0;i<l;i++){
      sum_alpha += alpha_block_sums[i];
    }
    std::cout << sum_alpha <<","; // SUM OF ALPHA

    std::pair<double,double> optim_vals = calc_WW_primal_obj(prob, nr_class, w, C);
    double primal_obj = optim_vals.first + optim_vals.second;
    double dual_obj = sum_alpha - optim_vals.first;
    if(iter == 0) dual_gap_init = primal_obj- dual_obj;
    std::cout << primal_obj << "\n"; // PRIMAL OBJ
    if((primal_obj - dual_obj) < STOPPING_CRITERION*dual_gap_init) break;
    SW.resume();
#endif

    iter++;
  }

	delete [] alpha;
  delete [] alpha_block_sums;
  
	delete [] alpha_new;
  delete [] del_alpha_pi;

  delete [] x_sq_norms;
  delete [] wxi;

  delete [] vSort;
  delete [] vIdx;

  delete [] v_pos;
  delete [] v_pos_idx;


}














// A reimplmentation of the Shark library's
// coordinate descent algorithm for
// multi-class support vector machines by Weston and Watkins.
//
// See https://github.com/Shark-ML/Shark
// under the file Shark/include/shark/Algorithms/QP/QpMcLinear.h
// for the original algorithm.
//
// UPDATE THIS!
//  min_{\alpha}  0.5 \sum_m ||w_m(\alpha)||^2 + \sum_i \sum_m e^m_i alpha^m_i
//    s.t.     \alpha^m_i <= C^m_i \forall m,i , \sum_m \alpha^m_i=0 \forall i
//
//  where e^m_i = 0 if y_i  = m,
//        e^m_i = 1 if y_i != m,
//  C^m_i = C if m  = y_i,
//  C^m_i = 0 if m != y_i,
//  and w_m(\alpha) = \sum_i \alpha^m_i x_i
//
// Given:
// x, y, C
// eps is the stopping tolerance
//
// solution will be put in w
class Solver_MCSVM_WW_Shark
{
  public:
		Solver_MCSVM_WW_Shark(const problem *prob, int nr_class, double C, double eps=0.1, int max_iter=5);
    ~Solver_MCSVM_WW_Shark();
    void Solve(double *w);
  private:
    double calcGradient(double * gradient, double * wxi, double * alpha, double C, unsigned int yi);
    void updateWeightVectors(double * w, double * mu, size_t index, double * step);
		void solveSub(double epsilon, double * gradient, double q, double C, unsigned int y, double * alpha, double * mu);

		double C;
		int w_size, l; // l is the number of instances
		int nr_class;
		int max_iter;
		double eps;
		const problem *prob;
};


Solver_MCSVM_WW_Shark::~Solver_MCSVM_WW_Shark()
{
}



Solver_MCSVM_WW_Shark::Solver_MCSVM_WW_Shark(const problem *prob, int nr_class, double C, double eps, int max_iter)
{
	this->w_size = prob->n;
	this->l = prob->l;
	this->nr_class = nr_class;
	this->eps = eps;
	this->max_iter = max_iter;
	this->prob = prob;
	this->C = C;
}


double Solver_MCSVM_WW_Shark::calcGradient(double * gradient, double * wx, double * alpha, double C, unsigned int y){
  double violation = 0.0;
  for(size_t c = 0; c < nr_class; c++){
    if(c == y)
    {
      gradient[c] = 0.0;
    }
    else
    {
/* #ifdef SHARK_FIX */
/*       const double g = 1.0 -  (wx[y] - wx[c]); */
/* #else */
      const double g = 1.0 -  0.5 * (wx[y] - wx[c]);
/* #endif */
      gradient[c] = g;
      if (g > violation && alpha[c] < C) violation = g;
      else if (-g > violation && alpha[c] > 0.0)  violation = -g;
    }
  }
  return violation;
}

void Solver_MCSVM_WW_Shark::updateWeightVectors(double * w, double * mu, size_t i, double * step){
  double sum_mu = 0.0;
  for(size_t c = 0; c < nr_class; c++) sum_mu += mu[c];
  int y_i = (int)prob->y[i];

  for(size_t s = 0; s<nr_class; s++){
      if(s == y_i){ step[s] = 0.5*sum_mu; }
      else{ step[s] = -0.5*mu[s]; }
  }

  // The following chunk is equivalent to the "add_scale" function from Shark's implementation
  const feature_node *xi;
  int fidx; // feature index
  for(size_t s=0; s<nr_class; s++){
    if(step[s]==0) continue;
    for(xi = prob->x[i]; (fidx=xi->index)!=-1;xi++){
      w[(fidx-1)*nr_class+s] += step[s]*xi->value;
    }
  }
  return;
}


// This function modifies: gradient, alpha and mu
void Solver_MCSVM_WW_Shark::solveSub(double epsilon, double * gradient, double q, double C, unsigned int y, double * alpha, double * mu){

  const double qq = 0.5 * q; // one-half times squared norm of x_i
  
  for(size_t iter = 0; iter < 10 * nr_class; iter++){
    size_t idx = 0;
    double kkt = 0.0;
    for(size_t c = 0; c < nr_class; c++){
      if (c == y) continue;

      const double g = gradient[c];
      const double a = alpha[c];

      if (g > kkt && a < C) { kkt = g; idx = c; }
      else if (-g > kkt && a > 0.0) { kkt = -g; idx = c; }
    }

    if (kkt < epsilon) break;

    const double a = alpha[idx];
    const double g = gradient[idx]; // negative gradient actually


    double m = g/qq;

    double a_new = a + m;
    if (a_new <= 0.0)
    {
      m = -a;
      a_new = 0.0;
    }
    else if (a_new >= C)
    {
      m = C - a;
      a_new = C;
    }


    alpha[idx] = a_new;
    mu[idx] += m;

    const double dg = 0.5 * m * qq;

    for(size_t c = 0; c < nr_class; c++) gradient[c] -= dg;
    gradient[idx] -= dg;
  }
  return;
}

void Solver_MCSVM_WW_Shark::Solve(double *w){

#ifdef TRACE_OPTIM_TRAJ
  stopwatch SW;
  double dual_gap_init;
#endif
  int i,s,j;
  int iter = 0;
  int idx;

	double *alpha =  new double[l*nr_class];
  double *g = new double[nr_class]; // the gradient
  double *mu = new double[nr_class];
  double * step = new double[nr_class];
  double *x_sq_norms = new double[l];
  double *wx = new double[nr_class];

  double *a; // a pointer to the current class
  double kkt;

  double epsilon = 0.1 * MAX_KKT_VIOLATION;

  const feature_node *xi;

  // Compute the squared norms of all the samples
  for(i=0;i<l;i++)
  {
    x_sq_norms[i] = 0;
    xi = prob->x[i];
    while(xi->index != -1)
    {
      double val = xi->value;
      x_sq_norms[i] += (val*val);
      xi++;
    }
  }
  
	int *index = new int[l];

	for(i=0;i<l;i++)
	{
		index[i] = i;
	}


	// Initialize alpha
	for(i=0;i<l*nr_class;i++)
		alpha[i] = 0.0;

  // Initialize w
	for(i=0;i<w_size*nr_class;i++)
		w[i] = 0.0;

  // outer loop
  while(iter<max_iter){

    // shuffle indices
		for(i=0;i<l;i++)
		{
			j = i+rand()%(l-i);
			my_swap(index[i], index[j]);
		}

    // inner loop
    for(j=0;j<l;j++){

      i = index[j];
      a = alpha + nr_class*i;
      int y_i = (int)prob->y[i];

      // reset the wxi and mu, populate the old alpha
      for(s=0;s<nr_class;s++){
        wx[s] = 0.0;
        mu[s] = 0.0;
      }

      // compute the k dimension vector w'xi
      for(xi = prob->x[i]; (idx=xi->index)!=-1;xi++){
        for(s=0;s<nr_class;s++){
          wx[s] += w[(idx-1)*nr_class+s]*xi->value;
        }
      }


      kkt = calcGradient(g, wx, a, C, y_i);

      double q = x_sq_norms[i];
      if(kkt > 0.0){
        solveSub(epsilon, g, q, C, y_i, a, mu);
        updateWeightVectors(w, mu, i, step);
      }

    }
#ifdef TRACE_OPTIM_TRAJ
    SW.pause();
    double elapsed_time = SW.get_time();
    std::cout << elapsed_time << ",";

  double sum_alpha = 0;
	for(i=0;i<l;i++){
    for(s = 0; s < nr_class; s++){
      int y_i = (int)prob->y[i];
      if(s == y_i) continue;
      sum_alpha += alpha[i*nr_class+s];
    }
  }
  std::cout << sum_alpha/4 <<",";

  for(i=0;i<nr_class*w_size;i++) w[i] *= (0.5);
  std::pair<double,double> optim_vals = calc_WW_primal_obj(prob, nr_class, w, C/4);
  double primal_obj = optim_vals.first + optim_vals.second;
  double dual_obj = sum_alpha/4 - optim_vals.first;
  for(i=0;i<nr_class*w_size;i++) w[i] *= 2;


  if(iter == 0) dual_gap_init = primal_obj - dual_obj;
  std::cout << primal_obj << "\n";
  if((primal_obj - dual_obj) < STOPPING_CRITERION*dual_gap_init) break;

  SW.resume();




#endif

    iter++;
  }
	delete [] alpha;
  delete [] g;
  delete [] mu;
  delete [] step;
  delete [] x_sq_norms;
  delete [] wx;
}



// A coordinate descent algorithm for
// multi-class support vector machines by Crammer and Singer
//
//  min_{\alpha}  0.5 \sum_m ||w_m(\alpha)||^2 + \sum_i \sum_m e^m_i alpha^m_i
//    s.t.     \alpha^m_i <= C^m_i \forall m,i , \sum_m \alpha^m_i=0 \forall i
//
//  where e^m_i = 0 if y_i  = m,
//        e^m_i = 1 if y_i != m,
//  C^m_i = C if m  = y_i,
//  C^m_i = 0 if m != y_i,
//  and w_m(\alpha) = \sum_i \alpha^m_i x_i
//
// Given:
// x, y, C
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Appendix of LIBLINEAR paper, Fan et al. (2008)

#define GETI(i) ((int) prob->y[i])
// To support weights for instances, use GETI(i) (i)

class Solver_MCSVM_CS
{
	public:
		Solver_MCSVM_CS(const problem *prob, int nr_class, double *C, double eps=0.1, int max_iter=100000);
		~Solver_MCSVM_CS();
		void Solve(double *w);
	private:
		void solve_sub_problem(double A_i, int yi, double C_yi, int active_i, double *alpha_new);
		bool be_shrunk(int i, int m, int yi, double alpha_i, double minG);
		double *B, *C, *G;
		int w_size, l;
		int nr_class;
		int max_iter;
		double eps;
		const problem *prob;
};

Solver_MCSVM_CS::Solver_MCSVM_CS(const problem *prob, int nr_class, double *weighted_C, double eps, int max_iter)
{
	this->w_size = prob->n;
	this->l = prob->l;
	this->nr_class = nr_class;
	this->eps = eps;
	this->max_iter = max_iter;
	this->prob = prob;
	this->B = new double[nr_class];
	this->G = new double[nr_class];
	this->C = weighted_C;
}

Solver_MCSVM_CS::~Solver_MCSVM_CS()
{
	delete[] B;
	delete[] G;
}

int compare_double(const void *a, const void *b)
{
	if(*(double *)a > *(double *)b)
		return -1;
	if(*(double *)a < *(double *)b)
		return 1;
	return 0;
}

void Solver_MCSVM_CS::solve_sub_problem(double A_i, int yi, double C_yi, int active_i, double *alpha_new)
{
	int r;
	double *D;

	clone(D, B, active_i);
	if(yi < active_i)
		D[yi] += A_i*C_yi;
	qsort(D, active_i, sizeof(double), compare_double);

	double beta = D[0] - A_i*C_yi;
	for(r=1;r<active_i && beta<r*D[r];r++)
		beta += D[r];
	beta /= r;

	for(r=0;r<active_i;r++)
	{
		if(r == yi)
			alpha_new[r] = min(C_yi, (beta-B[r])/A_i);
		else
			alpha_new[r] = min((double)0, (beta - B[r])/A_i);
	}
	delete[] D;
}

bool Solver_MCSVM_CS::be_shrunk(int i, int m, int yi, double alpha_i, double minG)
{
	double bound = 0;
	if(m == yi)
		bound = C[GETI(i)];
	if(alpha_i == bound && G[m] < minG)
		return true;
	return false;
}

void Solver_MCSVM_CS::Solve(double *w)
{
	int i, m, s;
	int iter = 0;
	double *alpha =  new double[l*nr_class];
	double *alpha_new = new double[nr_class];
	int *index = new int[l];
	double *QD = new double[l];
	int *d_ind = new int[nr_class];
	double *d_val = new double[nr_class];
	int *alpha_index = new int[nr_class*l];
	int *y_index = new int[l];
	int active_size = l;
	int *active_size_i = new int[l];
	double eps_shrink = max(10.0*eps, 1.0); // stopping tolerance for shrinking
	bool start_from_all = true;

	// Initial alpha can be set here. Note that
	// sum_m alpha[i*nr_class+m] = 0, for all i=1,...,l-1
	// alpha[i*nr_class+m] <= C[GETI(i)] if prob->y[i] == m
	// alpha[i*nr_class+m] <= 0 if prob->y[i] != m
	// If initial alpha isn't zero, uncomment the for loop below to initialize w
	for(i=0;i<l*nr_class;i++)
		alpha[i] = 0;

	for(i=0;i<w_size*nr_class;i++)
		w[i] = 0;
	for(i=0;i<l;i++)
	{
		for(m=0;m<nr_class;m++)
			alpha_index[i*nr_class+m] = m;
		feature_node *xi = prob->x[i];
		QD[i] = 0;
		while(xi->index != -1)
		{
			double val = xi->value;
			QD[i] += val*val;

			// Uncomment the for loop if initial alpha isn't zero
			// for(m=0; m<nr_class; m++)
			//	w[(xi->index-1)*nr_class+m] += alpha[i*nr_class+m]*val;
			xi++;
		}
		active_size_i[i] = nr_class;
		y_index[i] = (int)prob->y[i];
		index[i] = i;
	}

	while(iter < max_iter)
	{
		double stopping = -INF;
		for(i=0;i<active_size;i++)
		{
			int j = i+rand()%(active_size-i);
			my_swap(index[i], index[j]);
		}
		for(s=0;s<active_size;s++)
		{
			i = index[s];
			double Ai = QD[i];
			double *alpha_i = &alpha[i*nr_class];
			int *alpha_index_i = &alpha_index[i*nr_class];

			if(Ai > 0)
			{
				for(m=0;m<active_size_i[i];m++)
					G[m] = 1;
				if(y_index[i] < active_size_i[i])
					G[y_index[i]] = 0;

				feature_node *xi = prob->x[i];
				while(xi->index!= -1)
				{
					double *w_i = &w[(xi->index-1)*nr_class];
					for(m=0;m<active_size_i[i];m++)
						G[m] += w_i[alpha_index_i[m]]*(xi->value);
					xi++;
				}

				double minG = INF;
				double maxG = -INF;
				for(m=0;m<active_size_i[i];m++)
				{
					if(alpha_i[alpha_index_i[m]] < 0 && G[m] < minG)
						minG = G[m];
					if(G[m] > maxG)
						maxG = G[m];
				}
				if(y_index[i] < active_size_i[i])
					if(alpha_i[(int) prob->y[i]] < C[GETI(i)] && G[y_index[i]] < minG)
						minG = G[y_index[i]];

				for(m=0;m<active_size_i[i];m++)
				{
					if(be_shrunk(i, m, y_index[i], alpha_i[alpha_index_i[m]], minG))
					{
						active_size_i[i]--;
						while(active_size_i[i]>m)
						{
							if(!be_shrunk(i, active_size_i[i], y_index[i],
											alpha_i[alpha_index_i[active_size_i[i]]], minG))
							{
								my_swap(alpha_index_i[m], alpha_index_i[active_size_i[i]]);
								my_swap(G[m], G[active_size_i[i]]);
								if(y_index[i] == active_size_i[i])
									y_index[i] = m;
								else if(y_index[i] == m)
									y_index[i] = active_size_i[i];
								break;
							}
							active_size_i[i]--;
						}
					}
				}

				if(active_size_i[i] <= 1)
				{
					active_size--;
					my_swap(index[s], index[active_size]);
					s--;
					continue;
				}

				if(maxG-minG <= 1e-12)
					continue;
				else
					stopping = max(maxG - minG, stopping);

				for(m=0;m<active_size_i[i];m++)
					B[m] = G[m] - Ai*alpha_i[alpha_index_i[m]] ;

				solve_sub_problem(Ai, y_index[i], C[GETI(i)], active_size_i[i], alpha_new);
				int nz_d = 0;
				for(m=0;m<active_size_i[i];m++)
				{
					double d = alpha_new[m] - alpha_i[alpha_index_i[m]];
					alpha_i[alpha_index_i[m]] = alpha_new[m];
					if(fabs(d) >= 1e-12)
					{
						d_ind[nz_d] = alpha_index_i[m];
						d_val[nz_d] = d;
						nz_d++;
					}
				}

				xi = prob->x[i];
				while(xi->index != -1)
				{
					double *w_i = &w[(xi->index-1)*nr_class];
					for(m=0;m<nz_d;m++)
						w_i[d_ind[m]] += d_val[m]*xi->value;
					xi++;
				}
			}
		}

		iter++;
		if(iter % 10 == 0)
		{
			info(".");
		}

		if(stopping < eps_shrink)
		{
			if(stopping < eps && start_from_all == true)
				break;
			else
			{
				active_size = l;
				for(i=0;i<l;i++)
					active_size_i[i] = nr_class;
				info("*");
				eps_shrink = max(eps_shrink/2, eps);
				start_from_all = true;
			}
		}
		else
			start_from_all = false;
	}

	info("\noptimization finished, #iter = %d\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\n");

	// calculate objective value
	double v = 0;
	int nSV = 0;
	for(i=0;i<w_size*nr_class;i++)
		v += w[i]*w[i];
	v = 0.5*v;
	for(i=0;i<l*nr_class;i++)
	{
		v += alpha[i];
		if(fabs(alpha[i]) > 0)
			nSV++;
	}
	for(i=0;i<l;i++)
		v -= alpha[i*nr_class+(int)prob->y[i]];
	info("Objective value = %lf\n",v);
	info("nSV = %d\n",nSV);

	delete [] alpha;
	delete [] alpha_new;
	delete [] index;
	delete [] QD;
	delete [] d_ind;
	delete [] d_val;
	delete [] alpha_index;
	delete [] y_index;
	delete [] active_size_i;
}

// A coordinate descent algorithm for
// L1-loss and L2-loss SVM dual problems
//
//  min_\alpha  0.5(\alpha^T (Q + D)\alpha) - e^T \alpha,
//    s.t.      0 <= \alpha_i <= upper_bound_i,
//
//  where Qij = yi yj xi^T xj and
//  D is a diagonal matrix
//
// In L1-SVM case:
// 		upper_bound_i = Cp if y_i = 1
// 		upper_bound_i = Cn if y_i = -1
// 		D_ii = 0
// In L2-SVM case:
// 		upper_bound_i = INF
// 		D_ii = 1/(2*Cp)	if y_i = 1
// 		D_ii = 1/(2*Cn)	if y_i = -1
//
// Given:
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Algorithm 3 of Hsieh et al., ICML 2008

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

static void solve_l2r_l1l2_svc(
	const problem *prob, double *w, double eps,
	double Cp, double Cn, int solver_type)
{
	int l = prob->l;
	int w_size = prob->n;
	int i, s, iter = 0;
	double C, d, G;
	double *QD = new double[l];
	int max_iter = 1000;
	int *index = new int[l];
	double *alpha = new double[l];
	schar *y = new schar[l];
	int active_size = l;

	// PG: projected gradient, for shrinking and stopping
	double PG;
	double PGmax_old = INF;
	double PGmin_old = -INF;
	double PGmax_new, PGmin_new;

	// default solver_type: L2R_L2LOSS_SVC_DUAL
	double diag[3] = {0.5/Cn, 0, 0.5/Cp};
	double upper_bound[3] = {INF, 0, INF};
	if(solver_type == L2R_L1LOSS_SVC_DUAL)
	{
		diag[0] = 0;
		diag[2] = 0;
		upper_bound[0] = Cn;
		upper_bound[2] = Cp;
	}

	for(i=0; i<l; i++)
	{
		if(prob->y[i] > 0)
		{
			y[i] = +1;
		}
		else
		{
			y[i] = -1;
		}
	}

	// Initial alpha can be set here. Note that
	// 0 <= alpha[i] <= upper_bound[GETI(i)]
	for(i=0; i<l; i++)
		alpha[i] = 0;

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		QD[i] = diag[GETI(i)];

		feature_node * const xi = prob->x[i];
		QD[i] += sparse_operator::nrm2_sq(xi);
		sparse_operator::axpy(y[i]*alpha[i], xi, w);

		index[i] = i;
	}

	while (iter < max_iter)
	{
		PGmax_new = -INF;
		PGmin_new = INF;

		for (i=0; i<active_size; i++)
		{
			int j = i+rand()%(active_size-i);
			my_swap(index[i], index[j]);
		}

		for (s=0; s<active_size; s++)
		{
			i = index[s];
			const schar yi = y[i];
			feature_node * const xi = prob->x[i];

			G = yi*sparse_operator::dot(w, xi)-1;

			C = upper_bound[GETI(i)];
			G += alpha[i]*diag[GETI(i)];

			PG = 0;
			if (alpha[i] == 0)
			{
				if (G > PGmax_old)
				{
					active_size--;
					my_swap(index[s], index[active_size]);
					s--;
					continue;
				}
				else if (G < 0)
					PG = G;
			}
			else if (alpha[i] == C)
			{
				if (G < PGmin_old)
				{
					active_size--;
					my_swap(index[s], index[active_size]);
					s--;
					continue;
				}
				else if (G > 0)
					PG = G;
			}
			else
				PG = G;

			PGmax_new = max(PGmax_new, PG);
			PGmin_new = min(PGmin_new, PG);

			if(fabs(PG) > 1.0e-12)
			{
				double alpha_old = alpha[i];
				alpha[i] = min(max(alpha[i] - G/QD[i], 0.0), C);
				d = (alpha[i] - alpha_old)*yi;
				sparse_operator::axpy(d, xi, w);
			}
		}

		iter++;
		if(iter % 10 == 0)
			info(".");

		if(PGmax_new - PGmin_new <= eps)
		{
			if(active_size == l)
				break;
			else
			{
				active_size = l;
				info("*");
				PGmax_old = INF;
				PGmin_old = -INF;
				continue;
			}
		}
		PGmax_old = PGmax_new;
		PGmin_old = PGmin_new;
		if (PGmax_old <= 0)
			PGmax_old = INF;
		if (PGmin_old >= 0)
			PGmin_old = -INF;
	}

	info("\noptimization finished, #iter = %d\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\nUsing -s 2 may be faster (also see FAQ)\n\n");

	// calculate objective value

	double v = 0;
	int nSV = 0;
	for(i=0; i<w_size; i++)
		v += w[i]*w[i];
	for(i=0; i<l; i++)
	{
		v += alpha[i]*(alpha[i]*diag[GETI(i)] - 2);
		if(alpha[i] > 0)
			++nSV;
	}
	info("Objective value = %lf\n",v/2);
	info("nSV = %d\n",nSV);

	delete [] QD;
	delete [] alpha;
	delete [] y;
	delete [] index;
}


// A coordinate descent algorithm for
// L1-loss and L2-loss epsilon-SVR dual problem
//
//  min_\beta  0.5\beta^T (Q + diag(lambda)) \beta - p \sum_{i=1}^l|\beta_i| + \sum_{i=1}^l yi\beta_i,
//    s.t.      -upper_bound_i <= \beta_i <= upper_bound_i,
//
//  where Qij = xi^T xj and
//  D is a diagonal matrix
//
// In L1-SVM case:
// 		upper_bound_i = C
// 		lambda_i = 0
// In L2-SVM case:
// 		upper_bound_i = INF
// 		lambda_i = 1/(2*C)
//
// Given:
// x, y, p, C
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Algorithm 4 of Ho and Lin, 2012

#undef GETI
#define GETI(i) (0)
// To support weights for instances, use GETI(i) (i)

static void solve_l2r_l1l2_svr(
	const problem *prob, double *w, const parameter *param,
	int solver_type)
{
	int l = prob->l;
	double C = param->C;
	double p = param->p;
	int w_size = prob->n;
	double eps = param->eps;
	int i, s, iter = 0;
	int max_iter = 1000;
	int active_size = l;
	int *index = new int[l];

	double d, G, H;
	double Gmax_old = INF;
	double Gmax_new, Gnorm1_new;
	double Gnorm1_init = -1.0; // Gnorm1_init is initialized at the first iteration
	double *beta = new double[l];
	double *QD = new double[l];
	double *y = prob->y;

	// L2R_L2LOSS_SVR_DUAL
	double lambda[1], upper_bound[1];
	lambda[0] = 0.5/C;
	upper_bound[0] = INF;

	if(solver_type == L2R_L1LOSS_SVR_DUAL)
	{
		lambda[0] = 0;
		upper_bound[0] = C;
	}

	// Initial beta can be set here. Note that
	// -upper_bound <= beta[i] <= upper_bound
	for(i=0; i<l; i++)
		beta[i] = 0;

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		feature_node * const xi = prob->x[i];
		QD[i] = sparse_operator::nrm2_sq(xi);
		sparse_operator::axpy(beta[i], xi, w);

		index[i] = i;
	}


	while(iter < max_iter)
	{
		Gmax_new = 0;
		Gnorm1_new = 0;

		for(i=0; i<active_size; i++)
		{
			int j = i+rand()%(active_size-i);
			my_swap(index[i], index[j]);
		}

		for(s=0; s<active_size; s++)
		{
			i = index[s];
			G = -y[i] + lambda[GETI(i)]*beta[i];
			H = QD[i] + lambda[GETI(i)];

			feature_node * const xi = prob->x[i];
			G += sparse_operator::dot(w, xi);

			double Gp = G+p;
			double Gn = G-p;
			double violation = 0;
			if(beta[i] == 0)
			{
				if(Gp < 0)
					violation = -Gp;
				else if(Gn > 0)
					violation = Gn;
				else if(Gp>Gmax_old && Gn<-Gmax_old)
				{
					active_size--;
					my_swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(beta[i] >= upper_bound[GETI(i)])
			{
				if(Gp > 0)
					violation = Gp;
				else if(Gp < -Gmax_old)
				{
					active_size--;
					my_swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(beta[i] <= -upper_bound[GETI(i)])
			{
				if(Gn < 0)
					violation = -Gn;
				else if(Gn > Gmax_old)
				{
					active_size--;
					my_swap(index[s], index[active_size]);
					s--;
					continue;
				}
			}
			else if(beta[i] > 0)
				violation = fabs(Gp);
			else
				violation = fabs(Gn);

			Gmax_new = max(Gmax_new, violation);
			Gnorm1_new += violation;

			// obtain Newton direction d
			if(Gp < H*beta[i])
				d = -Gp/H;
			else if(Gn > H*beta[i])
				d = -Gn/H;
			else
				d = -beta[i];

			if(fabs(d) < 1.0e-12)
				continue;

			double beta_old = beta[i];
			beta[i] = min(max(beta[i]+d, -upper_bound[GETI(i)]), upper_bound[GETI(i)]);
			d = beta[i]-beta_old;

			if(d != 0)
				sparse_operator::axpy(d, xi, w);
		}

		if(iter == 0)
			Gnorm1_init = Gnorm1_new;
		iter++;
		if(iter % 10 == 0)
			info(".");

		if(Gnorm1_new <= eps*Gnorm1_init)
		{
			if(active_size == l)
				break;
			else
			{
				active_size = l;
				info("*");
				Gmax_old = INF;
				continue;
			}
		}

		Gmax_old = Gmax_new;
	}

	info("\noptimization finished, #iter = %d\n", iter);
	if(iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\nUsing -s 11 may be faster\n\n");

	// calculate objective value
	double v = 0;
	int nSV = 0;
	for(i=0; i<w_size; i++)
		v += w[i]*w[i];
	v = 0.5*v;
	for(i=0; i<l; i++)
	{
		v += p*fabs(beta[i]) - y[i]*beta[i] + 0.5*lambda[GETI(i)]*beta[i]*beta[i];
		if(beta[i] != 0)
			nSV++;
	}

	info("Objective value = %lf\n", v);
	info("nSV = %d\n",nSV);

	delete [] beta;
	delete [] QD;
	delete [] index;
}


// A coordinate descent algorithm for
// the dual of L2-regularized logistic regression problems
//
//  min_\alpha  0.5(\alpha^T Q \alpha) + \sum \alpha_i log (\alpha_i) + (upper_bound_i - \alpha_i) log (upper_bound_i - \alpha_i),
//    s.t.      0 <= \alpha_i <= upper_bound_i,
//
//  where Qij = yi yj xi^T xj and
//  upper_bound_i = Cp if y_i = 1
//  upper_bound_i = Cn if y_i = -1
//
// Given:
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Algorithm 5 of Yu et al., MLJ 2010

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

void solve_l2r_lr_dual(const problem *prob, double *w, double eps, double Cp, double Cn)
{
	int l = prob->l;
	int w_size = prob->n;
	int i, s, iter = 0;
	double *xTx = new double[l];
	int max_iter = 1000;
	int *index = new int[l];
	double *alpha = new double[2*l]; // store alpha and C - alpha
	schar *y = new schar[l];
	int max_inner_iter = 100; // for inner Newton
	double innereps = 1e-2;
	double innereps_min = min(1e-8, eps);
	double upper_bound[3] = {Cn, 0, Cp};

	for(i=0; i<l; i++)
	{
		if(prob->y[i] > 0)
		{
			y[i] = +1;
		}
		else
		{
			y[i] = -1;
		}
	}

	// Initial alpha can be set here. Note that
	// 0 < alpha[i] < upper_bound[GETI(i)]
	// alpha[2*i] + alpha[2*i+1] = upper_bound[GETI(i)]
	for(i=0; i<l; i++)
	{
		alpha[2*i] = min(0.001*upper_bound[GETI(i)], 1e-8);
		alpha[2*i+1] = upper_bound[GETI(i)] - alpha[2*i];
	}

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		feature_node * const xi = prob->x[i];
		xTx[i] = sparse_operator::nrm2_sq(xi);
		sparse_operator::axpy(y[i]*alpha[2*i], xi, w);
		index[i] = i;
	}

	while (iter < max_iter)
	{
		for (i=0; i<l; i++)
		{
			int j = i+rand()%(l-i);
			my_swap(index[i], index[j]);
		}
		int newton_iter = 0;
		double Gmax = 0;
		for (s=0; s<l; s++)
		{
			i = index[s];
			const schar yi = y[i];
			double C = upper_bound[GETI(i)];
			double ywTx = 0, xisq = xTx[i];
			feature_node * const xi = prob->x[i];
			ywTx = yi*sparse_operator::dot(w, xi);
			double a = xisq, b = ywTx;

			// Decide to minimize g_1(z) or g_2(z)
			int ind1 = 2*i, ind2 = 2*i+1, sign = 1;
			if(0.5*a*(alpha[ind2]-alpha[ind1])+b < 0)
			{
				ind1 = 2*i+1;
				ind2 = 2*i;
				sign = -1;
			}

			//  g_t(z) = z*log(z) + (C-z)*log(C-z) + 0.5a(z-alpha_old)^2 + sign*b(z-alpha_old)
			double alpha_old = alpha[ind1];
			double z = alpha_old;
			if(C - z < 0.5 * C)
				z = 0.1*z;
			double gp = a*(z-alpha_old)+sign*b+log(z/(C-z));
			Gmax = max(Gmax, fabs(gp));

			// Newton method on the sub-problem
			const double eta = 0.1; // xi in the paper
			int inner_iter = 0;
			while (inner_iter <= max_inner_iter)
			{
				if(fabs(gp) < innereps)
					break;
				double gpp = a + C/(C-z)/z;
				double tmpz = z - gp/gpp;
				if(tmpz <= 0)
					z *= eta;
				else // tmpz in (0, C)
					z = tmpz;
				gp = a*(z-alpha_old)+sign*b+log(z/(C-z));
				newton_iter++;
				inner_iter++;
			}

			if(inner_iter > 0) // update w
			{
				alpha[ind1] = z;
				alpha[ind2] = C-z;
				sparse_operator::axpy(sign*(z-alpha_old)*yi, xi, w);
			}
		}

		iter++;
		if(iter % 10 == 0)
			info(".");

		if(Gmax < eps)
			break;

		if(newton_iter <= l/10)
			innereps = max(innereps_min, 0.1*innereps);

	}

	info("\noptimization finished, #iter = %d\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\nUsing -s 0 may be faster (also see FAQ)\n\n");

	// calculate objective value

	double v = 0;
	for(i=0; i<w_size; i++)
		v += w[i] * w[i];
	v *= 0.5;
	for(i=0; i<l; i++)
		v += alpha[2*i] * log(alpha[2*i]) + alpha[2*i+1] * log(alpha[2*i+1])
			- upper_bound[GETI(i)] * log(upper_bound[GETI(i)]);
	info("Objective value = %lf\n", v);

	delete [] xTx;
	delete [] alpha;
	delete [] y;
	delete [] index;
}

// A coordinate descent algorithm for
// L1-regularized L2-loss support vector classification
//
//  min_w \sum |wj| + C \sum max(0, 1-yi w^T xi)^2,
//
// Given:
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Yuan et al. (2010) and appendix of LIBLINEAR paper, Fan et al. (2008)
//
// To not regularize the bias (i.e., regularize_bias = 0), a constant feature = 1
// must have been added to the original data. (see -B and -R option)

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

static void solve_l1r_l2_svc(
	problem *prob_col, double *w, double eps,
	double Cp, double Cn, int regularize_bias)
{
	int l = prob_col->l;
	int w_size = prob_col->n;
	int j, s, iter = 0;
	int max_iter = 1000;
	int active_size = w_size;
	int max_num_linesearch = 20;

	double sigma = 0.01;
	double d, G_loss, G, H;
	double Gmax_old = INF;
	double Gmax_new, Gnorm1_new;
	double Gnorm1_init = -1.0; // Gnorm1_init is initialized at the first iteration
	double d_old, d_diff;
	double loss_old = 0, loss_new;
	double appxcond, cond;

	int *index = new int[w_size];
	schar *y = new schar[l];
	double *b = new double[l]; // b = 1-ywTx
	double *xj_sq = new double[w_size];
	feature_node *x;

	double C[3] = {Cn,0,Cp};

	// Initial w can be set here.
	for(j=0; j<w_size; j++)
		w[j] = 0;

	for(j=0; j<l; j++)
	{
		b[j] = 1;
		if(prob_col->y[j] > 0)
			y[j] = 1;
		else
			y[j] = -1;
	}
	for(j=0; j<w_size; j++)
	{
		index[j] = j;
		xj_sq[j] = 0;
		x = prob_col->x[j];
		while(x->index != -1)
		{
			int ind = x->index-1;
			x->value *= y[ind]; // x->value stores yi*xij
			double val = x->value;
			b[ind] -= w[j]*val;
			xj_sq[j] += C[GETI(ind)]*val*val;
			x++;
		}
	}

	while(iter < max_iter)
	{
		Gmax_new = 0;
		Gnorm1_new = 0;

		for(j=0; j<active_size; j++)
		{
			int i = j+rand()%(active_size-j);
			my_swap(index[i], index[j]);
		}

		for(s=0; s<active_size; s++)
		{
			j = index[s];
			G_loss = 0;
			H = 0;

			x = prob_col->x[j];
			while(x->index != -1)
			{
				int ind = x->index-1;
				if(b[ind] > 0)
				{
					double val = x->value;
					double tmp = C[GETI(ind)]*val;
					G_loss -= tmp*b[ind];
					H += tmp*val;
				}
				x++;
			}
			G_loss *= 2;

			G = G_loss;
			H *= 2;
			H = max(H, 1e-12);

			double violation = 0;
			double Gp = 0, Gn = 0;
			if(j == w_size-1 && regularize_bias == 0)
				violation = fabs(G);
			else
			{
				Gp = G+1;
				Gn = G-1;
				if(w[j] == 0)
				{
					if(Gp < 0)
						violation = -Gp;
					else if(Gn > 0)
						violation = Gn;
					else if(Gp>Gmax_old/l && Gn<-Gmax_old/l)
					{
						active_size--;
						my_swap(index[s], index[active_size]);
						s--;
						continue;
					}
				}
				else if(w[j] > 0)
					violation = fabs(Gp);
				else
					violation = fabs(Gn);
			}
			Gmax_new = max(Gmax_new, violation);
			Gnorm1_new += violation;

			// obtain Newton direction d
			if(j == w_size-1 && regularize_bias == 0)
				d = -G/H;
			else
			{
				if(Gp < H*w[j])
					d = -Gp/H;
				else if(Gn > H*w[j])
					d = -Gn/H;
				else
					d = -w[j];
			}

			if(fabs(d) < 1.0e-12)
				continue;

			double delta;
			if(j == w_size-1 && regularize_bias == 0)
				delta = G*d;
			else
				delta = fabs(w[j]+d)-fabs(w[j]) + G*d;
			d_old = 0;
			int num_linesearch;
			for(num_linesearch=0; num_linesearch < max_num_linesearch; num_linesearch++)
			{
				d_diff = d_old - d;
				if(j == w_size-1 && regularize_bias == 0)
					cond = -sigma*delta;
				else
					cond = fabs(w[j]+d)-fabs(w[j]) - sigma*delta;

				appxcond = xj_sq[j]*d*d + G_loss*d + cond;
				if(appxcond <= 0)
				{
					x = prob_col->x[j];
					sparse_operator::axpy(d_diff, x, b);
					break;
				}

				if(num_linesearch == 0)
				{
					loss_old = 0;
					loss_new = 0;
					x = prob_col->x[j];
					while(x->index != -1)
					{
						int ind = x->index-1;
						if(b[ind] > 0)
							loss_old += C[GETI(ind)]*b[ind]*b[ind];
						double b_new = b[ind] + d_diff*x->value;
						b[ind] = b_new;
						if(b_new > 0)
							loss_new += C[GETI(ind)]*b_new*b_new;
						x++;
					}
				}
				else
				{
					loss_new = 0;
					x = prob_col->x[j];
					while(x->index != -1)
					{
						int ind = x->index-1;
						double b_new = b[ind] + d_diff*x->value;
						b[ind] = b_new;
						if(b_new > 0)
							loss_new += C[GETI(ind)]*b_new*b_new;
						x++;
					}
				}

				cond = cond + loss_new - loss_old;
				if(cond <= 0)
					break;
				else
				{
					d_old = d;
					d *= 0.5;
					delta *= 0.5;
				}
			}

			w[j] += d;

			// recompute b[] if line search takes too many steps
			if(num_linesearch >= max_num_linesearch)
			{
				info("#");
				for(int i=0; i<l; i++)
					b[i] = 1;

				for(int i=0; i<w_size; i++)
				{
					if(w[i]==0) continue;
					x = prob_col->x[i];
					sparse_operator::axpy(-w[i], x, b);
				}
			}
		}

		if(iter == 0)
			Gnorm1_init = Gnorm1_new;
		iter++;
		if(iter % 10 == 0)
			info(".");

		if(Gnorm1_new <= eps*Gnorm1_init)
		{
			if(active_size == w_size)
				break;
			else
			{
				active_size = w_size;
				info("*");
				Gmax_old = INF;
				continue;
			}
		}

		Gmax_old = Gmax_new;
	}

	info("\noptimization finished, #iter = %d\n", iter);
	if(iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\n");

	// calculate objective value

	double v = 0;
	int nnz = 0;
	for(j=0; j<w_size; j++)
	{
		x = prob_col->x[j];
		while(x->index != -1)
		{
			x->value *= prob_col->y[x->index-1]; // restore x->value
			x++;
		}
		if(w[j] != 0)
		{
			v += fabs(w[j]);
			nnz++;
		}
	}
	if (regularize_bias == 0)
		v -= fabs(w[w_size-1]);
	for(j=0; j<l; j++)
		if(b[j] > 0)
			v += C[GETI(j)]*b[j]*b[j];

	info("Objective value = %lf\n", v);
	info("#nonzeros/#features = %d/%d\n", nnz, w_size);

	delete [] index;
	delete [] y;
	delete [] b;
	delete [] xj_sq;
}

// A coordinate descent algorithm for
// L1-regularized logistic regression problems
//
//  min_w \sum |wj| + C \sum log(1+exp(-yi w^T xi)),
//
// Given:
// x, y, Cp, Cn
// eps is the stopping tolerance
//
// solution will be put in w
//
// See Yuan et al. (2011) and appendix of LIBLINEAR paper, Fan et al. (2008)
//
// To not regularize the bias (i.e., regularize_bias = 0), a constant feature = 1
// must have been added to the original data. (see -B and -R option)

#undef GETI
#define GETI(i) (y[i]+1)
// To support weights for instances, use GETI(i) (i)

static void solve_l1r_lr(
	const problem *prob_col, double *w, double eps,
	double Cp, double Cn, int regularize_bias)
{
	int l = prob_col->l;
	int w_size = prob_col->n;
	int j, s, newton_iter=0, iter=0;
	int max_newton_iter = 100;
	int max_iter = 1000;
	int max_num_linesearch = 20;
	int active_size;
	int QP_active_size;

	double nu = 1e-12;
	double inner_eps = 1;
	double sigma = 0.01;
	double w_norm, w_norm_new;
	double z, G, H;
	double Gnorm1_init = -1.0; // Gnorm1_init is initialized at the first iteration
	double Gmax_old = INF;
	double Gmax_new, Gnorm1_new;
	double QP_Gmax_old = INF;
	double QP_Gmax_new, QP_Gnorm1_new;
	double delta, negsum_xTd, cond;

	int *index = new int[w_size];
	schar *y = new schar[l];
	double *Hdiag = new double[w_size];
	double *Grad = new double[w_size];
	double *wpd = new double[w_size];
	double *xjneg_sum = new double[w_size];
	double *xTd = new double[l];
	double *exp_wTx = new double[l];
	double *exp_wTx_new = new double[l];
	double *tau = new double[l];
	double *D = new double[l];
	feature_node *x;

	double C[3] = {Cn,0,Cp};

	// Initial w can be set here.
	for(j=0; j<w_size; j++)
		w[j] = 0;

	for(j=0; j<l; j++)
	{
		if(prob_col->y[j] > 0)
			y[j] = 1;
		else
			y[j] = -1;

		exp_wTx[j] = 0;
	}

	w_norm = 0;
	for(j=0; j<w_size; j++)
	{
		w_norm += fabs(w[j]);
		wpd[j] = w[j];
		index[j] = j;
		xjneg_sum[j] = 0;
		x = prob_col->x[j];
		while(x->index != -1)
		{
			int ind = x->index-1;
			double val = x->value;
			exp_wTx[ind] += w[j]*val;
			if(y[ind] == -1)
				xjneg_sum[j] += C[GETI(ind)]*val;
			x++;
		}
	}
	if (regularize_bias == 0)
		w_norm -= fabs(w[w_size-1]);

	for(j=0; j<l; j++)
	{
		exp_wTx[j] = exp(exp_wTx[j]);
		double tau_tmp = 1/(1+exp_wTx[j]);
		tau[j] = C[GETI(j)]*tau_tmp;
		D[j] = C[GETI(j)]*exp_wTx[j]*tau_tmp*tau_tmp;
	}

	while(newton_iter < max_newton_iter)
	{
		Gmax_new = 0;
		Gnorm1_new = 0;
		active_size = w_size;

		for(s=0; s<active_size; s++)
		{
			j = index[s];
			Hdiag[j] = nu;
			Grad[j] = 0;

			double tmp = 0;
			x = prob_col->x[j];
			while(x->index != -1)
			{
				int ind = x->index-1;
				Hdiag[j] += x->value*x->value*D[ind];
				tmp += x->value*tau[ind];
				x++;
			}
			Grad[j] = -tmp + xjneg_sum[j];

			double violation = 0;
			if (j == w_size-1 && regularize_bias == 0)
				violation = fabs(Grad[j]);
			else
			{
				double Gp = Grad[j]+1;
				double Gn = Grad[j]-1;
				if(w[j] == 0)
				{
					if(Gp < 0)
						violation = -Gp;
					else if(Gn > 0)
						violation = Gn;
					//outer-level shrinking
					else if(Gp>Gmax_old/l && Gn<-Gmax_old/l)
					{
						active_size--;
						my_swap(index[s], index[active_size]);
						s--;
						continue;
					}
				}
				else if(w[j] > 0)
					violation = fabs(Gp);
				else
					violation = fabs(Gn);
			}
			Gmax_new = max(Gmax_new, violation);
			Gnorm1_new += violation;
		}

		if(newton_iter == 0)
			Gnorm1_init = Gnorm1_new;

		if(Gnorm1_new <= eps*Gnorm1_init)
			break;

		iter = 0;
		QP_Gmax_old = INF;
		QP_active_size = active_size;

		for(int i=0; i<l; i++)
			xTd[i] = 0;

		// optimize QP over wpd
		while(iter < max_iter)
		{
			QP_Gmax_new = 0;
			QP_Gnorm1_new = 0;

			for(j=0; j<QP_active_size; j++)
			{
				int i = j+rand()%(QP_active_size-j);
				my_swap(index[i], index[j]);
			}

			for(s=0; s<QP_active_size; s++)
			{
				j = index[s];
				H = Hdiag[j];

				x = prob_col->x[j];
				G = Grad[j] + (wpd[j]-w[j])*nu;
				while(x->index != -1)
				{
					int ind = x->index-1;
					G += x->value*D[ind]*xTd[ind];
					x++;
				}

				double violation = 0;
				if (j == w_size-1 && regularize_bias == 0)
				{
					// bias term not shrunken
					violation = fabs(G);
					z = -G/H;
				}
				else
				{
					double Gp = G+1;
					double Gn = G-1;
					if(wpd[j] == 0)
					{
						if(Gp < 0)
							violation = -Gp;
						else if(Gn > 0)
							violation = Gn;
						//inner-level shrinking
						else if(Gp>QP_Gmax_old/l && Gn<-QP_Gmax_old/l)
						{
							QP_active_size--;
							my_swap(index[s], index[QP_active_size]);
							s--;
							continue;
						}
					}
					else if(wpd[j] > 0)
						violation = fabs(Gp);
					else
						violation = fabs(Gn);

					// obtain solution of one-variable problem
					if(Gp < H*wpd[j])
						z = -Gp/H;
					else if(Gn > H*wpd[j])
						z = -Gn/H;
					else
						z = -wpd[j];
				}
				QP_Gmax_new = max(QP_Gmax_new, violation);
				QP_Gnorm1_new += violation;

				if(fabs(z) < 1.0e-12)
					continue;
				z = min(max(z,-10.0),10.0);

				wpd[j] += z;

				x = prob_col->x[j];
				sparse_operator::axpy(z, x, xTd);
			}

			iter++;

			if(QP_Gnorm1_new <= inner_eps*Gnorm1_init)
			{
				//inner stopping
				if(QP_active_size == active_size)
					break;
				//active set reactivation
				else
				{
					QP_active_size = active_size;
					QP_Gmax_old = INF;
					continue;
				}
			}

			QP_Gmax_old = QP_Gmax_new;
		}

		if(iter >= max_iter)
			info("WARNING: reaching max number of inner iterations\n");

		delta = 0;
		w_norm_new = 0;
		for(j=0; j<w_size; j++)
		{
			delta += Grad[j]*(wpd[j]-w[j]);
			if(wpd[j] != 0)
				w_norm_new += fabs(wpd[j]);
		}
		if (regularize_bias == 0)
			w_norm_new -= fabs(wpd[w_size-1]);
		delta += (w_norm_new-w_norm);

		negsum_xTd = 0;
		for(int i=0; i<l; i++)
			if(y[i] == -1)
				negsum_xTd += C[GETI(i)]*xTd[i];

		int num_linesearch;
		for(num_linesearch=0; num_linesearch < max_num_linesearch; num_linesearch++)
		{
			cond = w_norm_new - w_norm + negsum_xTd - sigma*delta;

			for(int i=0; i<l; i++)
			{
				double exp_xTd = exp(xTd[i]);
				exp_wTx_new[i] = exp_wTx[i]*exp_xTd;
				cond += C[GETI(i)]*log((1+exp_wTx_new[i])/(exp_xTd+exp_wTx_new[i]));
			}

			if(cond <= 0)
			{
				w_norm = w_norm_new;
				for(j=0; j<w_size; j++)
					w[j] = wpd[j];
				for(int i=0; i<l; i++)
				{
					exp_wTx[i] = exp_wTx_new[i];
					double tau_tmp = 1/(1+exp_wTx[i]);
					tau[i] = C[GETI(i)]*tau_tmp;
					D[i] = C[GETI(i)]*exp_wTx[i]*tau_tmp*tau_tmp;
				}
				break;
			}
			else
			{
				w_norm_new = 0;
				for(j=0; j<w_size; j++)
				{
					wpd[j] = (w[j]+wpd[j])*0.5;
					if(wpd[j] != 0)
						w_norm_new += fabs(wpd[j]);
				}
				if (regularize_bias == 0)
					w_norm_new -= fabs(wpd[w_size-1]);
				delta *= 0.5;
				negsum_xTd *= 0.5;
				for(int i=0; i<l; i++)
					xTd[i] *= 0.5;
			}
		}

		// Recompute some info due to too many line search steps
		if(num_linesearch >= max_num_linesearch)
		{
			for(int i=0; i<l; i++)
				exp_wTx[i] = 0;

			for(int i=0; i<w_size; i++)
			{
				if(w[i]==0) continue;
				x = prob_col->x[i];
				sparse_operator::axpy(w[i], x, exp_wTx);
			}

			for(int i=0; i<l; i++)
				exp_wTx[i] = exp(exp_wTx[i]);
		}

		if(iter == 1)
			inner_eps *= 0.25;

		newton_iter++;
		Gmax_old = Gmax_new;

		info("iter %3d  #CD cycles %d\n", newton_iter, iter);
	}

	info("=========================\n");
	info("optimization finished, #iter = %d\n", newton_iter);
	if(newton_iter >= max_newton_iter)
		info("WARNING: reaching max number of iterations\n");

	// calculate objective value

	double v = 0;
	int nnz = 0;
	for(j=0; j<w_size; j++)
		if(w[j] != 0)
		{
			v += fabs(w[j]);
			nnz++;
		}
	if (regularize_bias == 0)
		v -= fabs(w[w_size-1]);
	for(j=0; j<l; j++)
		if(y[j] == 1)
			v += C[GETI(j)]*log(1+1/exp_wTx[j]);
		else
			v += C[GETI(j)]*log(1+exp_wTx[j]);

	info("Objective value = %lf\n", v);
	info("#nonzeros/#features = %d/%d\n", nnz, w_size);

	delete [] index;
	delete [] y;
	delete [] Hdiag;
	delete [] Grad;
	delete [] wpd;
	delete [] xjneg_sum;
	delete [] xTd;
	delete [] exp_wTx;
	delete [] exp_wTx_new;
	delete [] tau;
	delete [] D;
}

struct heap {
	enum HEAP_TYPE { MIN, MAX };
	int _size;
	HEAP_TYPE _type;
	feature_node* a;

	heap(int max_size, HEAP_TYPE type)
	{
		_size = 0;
		a = new feature_node[max_size];
		_type = type;
	}
	~heap()
	{
		delete [] a;
	}
	bool cmp(const feature_node& left, const feature_node& right)
	{
		if(_type == MIN)
			return left.value > right.value;
		else
			return left.value < right.value;
	}
	int size()
	{
		return _size;
	}
	void push(feature_node node)
	{
		a[_size] = node;
		_size++;
		int i = _size-1;
		while(i)
		{
			int p = (i-1)/2;
			if(cmp(a[p], a[i]))
			{
				my_swap(a[i], a[p]);
				i = p;
			}
			else
				break;
		}
	}
	void pop()
	{
		_size--;
		a[0] = a[_size];
		int i = 0;
		while(i*2+1 < _size)
		{
			int l = i*2+1;
			int r = i*2+2;
			if(r < _size && cmp(a[l], a[r]))
				l = r;
			if(cmp(a[i], a[l]))
			{
				my_swap(a[i], a[l]);
				i = l;
			}
			else
				break;
		}
	}
	feature_node top()
	{
		return a[0];
	}
};

// A two-level coordinate descent algorithm for
// a scaled one-class SVM dual problem
//
//  min_\alpha  0.5(\alpha^T Q \alpha),
//    s.t.      0 <= \alpha_i <= 1 and
//              e^T \alpha = \nu l
//
//  where Qij = xi^T xj
//
// Given:
// x, nu
// eps is the stopping tolerance
//
// solution will be put in w and rho
//
// See Algorithm 7 in supplementary materials of Chou et al., SDM 2020.

static void solve_oneclass_svm(const problem *prob, double *w, double *rho, double eps, double nu)
{
	int l = prob->l;
	int w_size = prob->n;
	int i, j, s, iter = 0;
	double Gi, Gj;
	double Qij, quad_coef, delta, sum;
	double old_alpha_i;
	double *QD = new double[l];
	double *G = new double[l];
	int *index = new int[l];
	double *alpha = new double[l];
	int max_inner_iter;
	int max_iter = 1000;
	int active_size = l;

	double negGmax;			// max { -grad(f)_i | alpha_i < 1 }
	double negGmin;			// min { -grad(f)_i | alpha_i > 0 }

	int *most_violating_i = new int[l];
	int *most_violating_j = new int[l];

	int n = (int)(nu*l);		// # of alpha's at upper bound
	for(i=0; i<n; i++)
		alpha[i] = 1;
	if (n<l)
		alpha[i] = nu*l-n;
	for(i=n+1; i<l; i++)
		alpha[i] = 0;

	for(i=0; i<w_size; i++)
		w[i] = 0;
	for(i=0; i<l; i++)
	{
		feature_node * const xi = prob->x[i];
		QD[i] = sparse_operator::nrm2_sq(xi);
		sparse_operator::axpy(alpha[i], xi, w);

		index[i] = i;
	}

	while (iter < max_iter)
	{
		negGmax = -INF;
		negGmin = INF;

		for (s=0; s<active_size; s++)
		{
			i = index[s];
			feature_node * const xi = prob->x[i];
			G[i] = sparse_operator::dot(w, xi);
			if (alpha[i] < 1)
				negGmax = max(negGmax, -G[i]);
			if (alpha[i] > 0)
				negGmin = min(negGmin, -G[i]);
		}

		if (negGmax - negGmin < eps)
		{
			if (active_size == l)
				break;
			else
			{
				active_size = l;
				info("*");
				continue;
			}
		}

		for(s=0; s<active_size; s++)
		{
			i = index[s];
			if ((alpha[i] == 1 && -G[i] > negGmax) ||
			    (alpha[i] == 0 && -G[i] < negGmin))
			{
				active_size--;
				my_swap(index[s], index[active_size]);
				s--;
			}
		}

		max_inner_iter = max(active_size/10, 1);
		struct heap min_heap = heap(max_inner_iter, heap::MIN);
		struct heap max_heap = heap(max_inner_iter, heap::MAX);
		struct feature_node node;
		for(s=0; s<active_size; s++)
		{
			i = index[s];
			node.index = i;
			node.value = -G[i];

			if (alpha[i] < 1)
			{
				if (min_heap.size() < max_inner_iter)
					min_heap.push(node);
				else if (min_heap.top().value < node.value)
				{
					min_heap.pop();
					min_heap.push(node);
				}
			}

			if (alpha[i] > 0)
			{
				if (max_heap.size() < max_inner_iter)
					max_heap.push(node);
				else if (max_heap.top().value > node.value)
				{
					max_heap.pop();
					max_heap.push(node);
				}
			}
		}
		max_inner_iter = min(min_heap.size(), max_heap.size());
		while (max_heap.size() > max_inner_iter)
			max_heap.pop();
		while (min_heap.size() > max_inner_iter)
			min_heap.pop();

		for (s=max_inner_iter-1; s>=0; s--)
		{
			most_violating_i[s] = min_heap.top().index;
			most_violating_j[s] = max_heap.top().index;
			min_heap.pop();
			max_heap.pop();
		}

		for (s=0; s<max_inner_iter; s++)
		{
			i = most_violating_i[s];
			j = most_violating_j[s];

			if ((alpha[i] == 0 && alpha[j] == 0) ||
			    (alpha[i] == 1 && alpha[j] == 1))
				continue;

			feature_node const * xi = prob->x[i];
			feature_node const * xj = prob->x[j];

			Gi = sparse_operator::dot(w, xi);
			Gj = sparse_operator::dot(w, xj);

			int violating_pair = 0;
			if (alpha[i] < 1 && alpha[j] > 0 && -Gj + 1e-12 < -Gi)
				violating_pair = 1;
			else
				if (alpha[i] > 0 && alpha[j] < 1 && -Gi + 1e-12 < -Gj)
					violating_pair = 1;
			if (violating_pair == 0)
				continue;

			Qij = sparse_operator::sparse_dot(xi, xj);
			quad_coef = QD[i] + QD[j] - 2*Qij;
			if(quad_coef <= 0)
				quad_coef = 1e-12;
			delta = (Gi - Gj) / quad_coef;
			old_alpha_i = alpha[i];
			sum = alpha[i] + alpha[j];
			alpha[i] = alpha[i] - delta;
			alpha[j] = alpha[j] + delta;
			if (sum > 1)
			{
				if (alpha[i] > 1)
				{
					alpha[i] = 1;
					alpha[j] = sum - 1;
				}
			}
			else
			{
				if (alpha[j] < 0)
				{
					alpha[j] = 0;
					alpha[i] = sum;
				}
			}
			if (sum > 1)
			{
				if (alpha[j] > 1)
				{
					alpha[j] = 1;
					alpha[i] = sum - 1;
				}
			}
			else
			{
				if (alpha[i] < 0)
				{
					alpha[i] = 0;
					alpha[j] = sum;
				}
			}
			delta = alpha[i] - old_alpha_i;
			sparse_operator::axpy(delta, xi, w);
			sparse_operator::axpy(-delta, xj, w);
		}
		iter++;
		if (iter % 10 == 0)
			info(".");
	}
	info("\noptimization finished, #iter = %d\n",iter);
	if (iter >= max_iter)
		info("\nWARNING: reaching max number of iterations\n\n");

	// calculate object value
	double v = 0;
	for(i=0; i<w_size; i++)
		v += w[i]*w[i];
	int nSV = 0;
	for(i=0; i<l; i++)
	{
		if (alpha[i] > 0)
			++nSV;
	}
	info("Objective value = %lf\n", v/2);
	info("nSV = %d\n", nSV);

	// calculate rho
	double nr_free = 0;
	double ub = INF, lb = -INF, sum_free = 0;
	for(i=0; i<l; i++)
	{
		double G = sparse_operator::dot(w, prob->x[i]);
		if (alpha[i] == 1)
			lb = max(lb, G);
		else if (alpha[i] == 0)
			ub = min(ub, G);
		else
		{
			++nr_free;
			sum_free += G;
		}
	}

	if (nr_free > 0)
		*rho = sum_free/nr_free;
	else
		*rho = (ub + lb)/2;

	info("rho = %lf\n", *rho);

	delete [] QD;
	delete [] G;
	delete [] index;
	delete [] alpha;
	delete [] most_violating_i;
	delete [] most_violating_j;
}

// transpose matrix X from row format to column format
static void transpose(const problem *prob, feature_node **x_space_ret, problem *prob_col)
{
	int i;
	int l = prob->l;
	int n = prob->n;
	size_t nnz = 0;
	size_t *col_ptr = new size_t [n+1];
	feature_node *x_space;
	prob_col->l = l;
	prob_col->n = n;
	prob_col->y = new double[l];
	prob_col->x = new feature_node*[n];

	for(i=0; i<l; i++)
		prob_col->y[i] = prob->y[i];

	for(i=0; i<n+1; i++)
		col_ptr[i] = 0;
	for(i=0; i<l; i++)
	{
		feature_node *x = prob->x[i];
		while(x->index != -1)
		{
			nnz++;
			col_ptr[x->index]++;
			x++;
		}
	}
	for(i=1; i<n+1; i++)
		col_ptr[i] += col_ptr[i-1] + 1;

	x_space = new feature_node[nnz+n];
	for(i=0; i<n; i++)
		prob_col->x[i] = &x_space[col_ptr[i]];

	for(i=0; i<l; i++)
	{
		feature_node *x = prob->x[i];
		while(x->index != -1)
		{
			int ind = x->index-1;
			x_space[col_ptr[ind]].index = i+1; // starts from 1
			x_space[col_ptr[ind]].value = x->value;
			col_ptr[ind]++;
			x++;
		}
	}
	for(i=0; i<n; i++)
		x_space[col_ptr[i]].index = -1;

	*x_space_ret = x_space;

	delete [] col_ptr;
}

// label: label name, start: begin of each class, count: #data of classes, perm: indices to the original data
// perm, length l, must be allocated before calling this subroutine
static void group_classes(const problem *prob, int *nr_class_ret, int **label_ret, int **start_ret, int **count_ret, int *perm)
{
	int l = prob->l;
	int max_nr_class = 16;
	int nr_class = 0;
	int *label = Malloc(int,max_nr_class);
	int *count = Malloc(int,max_nr_class);
	int *data_label = Malloc(int,l);
	int i;

	for(i=0;i<l;i++)
	{
		int this_label = (int)prob->y[i];
		int j;
		for(j=0;j<nr_class;j++)
		{
			if(this_label == label[j])
			{
				++count[j];
				break;
			}
		}
		data_label[i] = j;
		if(j == nr_class)
		{
			if(nr_class == max_nr_class)
			{
				max_nr_class *= 2;
				label = (int *)realloc(label,max_nr_class*sizeof(int));
				count = (int *)realloc(count,max_nr_class*sizeof(int));
			}
			label[nr_class] = this_label;
			count[nr_class] = 1;
			++nr_class;
		}
	}

	//
	// Labels are ordered by their first occurrence in the training set.
	// However, for two-class sets with -1/+1 labels and -1 appears first,
	// we my_swap labels to ensure that internally the binary SVM has positive data corresponding to the +1 instances.
	//
	if (nr_class == 2 && label[0] == -1 && label[1] == 1)
	{
		my_swap(label[0],label[1]);
		my_swap(count[0],count[1]);
		for(i=0;i<l;i++)
		{
			if(data_label[i] == 0)
				data_label[i] = 1;
			else
				data_label[i] = 0;
		}
	}

	int *start = Malloc(int,nr_class);
	start[0] = 0;
	for(i=1;i<nr_class;i++)
		start[i] = start[i-1]+count[i-1];
	for(i=0;i<l;i++)
	{
		perm[start[data_label[i]]] = i;
		++start[data_label[i]];
	}
	start[0] = 0;
	for(i=1;i<nr_class;i++)
		start[i] = start[i-1]+count[i-1];

	*nr_class_ret = nr_class;
	*label_ret = label;
	*start_ret = start;
	*count_ret = count;
	free(data_label);
}

static void train_one(const problem *prob, const parameter *param, double *w, double Cp, double Cn)
{
	double eps = param->eps;

	int pos = 0;
	int neg = 0;
	for(int i=0;i<prob->l;i++)
		if(prob->y[i] > 0)
			pos++;
	neg = prob->l - pos;
	double primal_solver_tol = eps*max(min(pos,neg), 1)/prob->l;

	function *fun_obj=NULL;
	switch(param->solver_type)
	{
		case L2R_LR:
		{
			double *C = new double[prob->l];
			for(int i = 0; i < prob->l; i++)
			{
				if(prob->y[i] > 0)
					C[i] = Cp;
				else
					C[i] = Cn;
			}
			fun_obj=new l2r_lr_fun(prob, param, C);
			NEWTON newton_obj(fun_obj, primal_solver_tol);
			newton_obj.set_print_string(liblinear_print_string);
			newton_obj.newton(w);
			delete fun_obj;
			delete[] C;
			break;
		}
		case L2R_L2LOSS_SVC:
		{
			double *C = new double[prob->l];
			for(int i = 0; i < prob->l; i++)
			{
				if(prob->y[i] > 0)
					C[i] = Cp;
				else
					C[i] = Cn;
			}
			fun_obj=new l2r_l2_svc_fun(prob, param, C);
			NEWTON newton_obj(fun_obj, primal_solver_tol);
			newton_obj.set_print_string(liblinear_print_string);
			newton_obj.newton(w);
			delete fun_obj;
			delete[] C;
			break;
		}
		case L2R_L2LOSS_SVC_DUAL:
			solve_l2r_l1l2_svc(prob, w, eps, Cp, Cn, L2R_L2LOSS_SVC_DUAL);
			break;
		case L2R_L1LOSS_SVC_DUAL:
			solve_l2r_l1l2_svc(prob, w, eps, Cp, Cn, L2R_L1LOSS_SVC_DUAL);
			break;
		case L1R_L2LOSS_SVC:
		{
			problem prob_col;
			feature_node *x_space = NULL;
			transpose(prob, &x_space ,&prob_col);
			solve_l1r_l2_svc(&prob_col, w, primal_solver_tol, Cp, Cn, param->regularize_bias);
			delete [] prob_col.y;
			delete [] prob_col.x;
			delete [] x_space;
			break;
		}
		case L1R_LR:
		{
			problem prob_col;
			feature_node *x_space = NULL;
			transpose(prob, &x_space ,&prob_col);
			solve_l1r_lr(&prob_col, w, primal_solver_tol, Cp, Cn, param->regularize_bias);
			delete [] prob_col.y;
			delete [] prob_col.x;
			delete [] x_space;
			break;
		}
		case L2R_LR_DUAL:
			solve_l2r_lr_dual(prob, w, eps, Cp, Cn);
			break;
		case L2R_L2LOSS_SVR:
		{
			double *C = new double[prob->l];
			for(int i = 0; i < prob->l; i++)
				C[i] = param->C;

			fun_obj=new l2r_l2_svr_fun(prob, param, C);
			NEWTON newton_obj(fun_obj, param->eps);
			newton_obj.set_print_string(liblinear_print_string);
			newton_obj.newton(w);
			delete fun_obj;
			delete[] C;
			break;

		}
		case L2R_L1LOSS_SVR_DUAL:
			solve_l2r_l1l2_svr(prob, w, param, L2R_L1LOSS_SVR_DUAL);
			break;
		case L2R_L2LOSS_SVR_DUAL:
			solve_l2r_l1l2_svr(prob, w, param, L2R_L2LOSS_SVR_DUAL);
			break;
		default:
			fprintf(stderr, "ERROR: unknown solver_type\n");
			break;
	}
}

// Calculate the initial C for parameter selection
static double calc_start_C(const problem *prob, const parameter *param)
{
	int i;
	double xTx, max_xTx;
	max_xTx = 0;
	for(i=0; i<prob->l; i++)
	{
		xTx = 0;
		feature_node *xi=prob->x[i];
		while(xi->index != -1)
		{
			double val = xi->value;
			xTx += val*val;
			xi++;
		}
		if(xTx > max_xTx)
			max_xTx = xTx;
	}

	double min_C = 1.0;
	if(param->solver_type == L2R_LR)
		min_C = 1.0 / (prob->l * max_xTx);
	else if(param->solver_type == L2R_L2LOSS_SVC)
		min_C = 1.0 / (2 * prob->l * max_xTx);
	else if(param->solver_type == L2R_L2LOSS_SVR)
	{
		double sum_y, loss, y_abs;
		double delta2 = 0.1;
		sum_y = 0, loss = 0;
		for(i=0; i<prob->l; i++)
		{
			y_abs = fabs(prob->y[i]);
			sum_y += y_abs;
			loss += max(y_abs - param->p, 0.0) * max(y_abs - param->p, 0.0);
		}
		if(loss > 0)
			min_C = delta2 * delta2 * loss / (8 * sum_y * sum_y * max_xTx);
		else
			min_C = INF;
	}

	return pow( 2, floor(log(min_C) / log(2.0)) );
}

static double calc_max_p(const problem *prob, const parameter *param)
{
	int i;
	double max_p = 0.0;
	for(i = 0; i < prob->l; i++)
		max_p = max(max_p, fabs(prob->y[i]));

	return max_p;
}

static void find_parameter_C(const problem *prob, parameter *param_tmp, double start_C, double max_C, double *best_C, double *best_score, const int *fold_start, const int *perm, const problem *subprob, int nr_fold)
{
	// variables for CV
	int i;
	double *target = Malloc(double, prob->l);

	// variables for warm start
	double ratio = 2;
	double **prev_w = Malloc(double*, nr_fold);
	for(i = 0; i < nr_fold; i++)
		prev_w[i] = NULL;
	int num_unchanged_w = 0;
	void (*default_print_string) (const char *) = liblinear_print_string;

	if(param_tmp->solver_type == L2R_LR || param_tmp->solver_type == L2R_L2LOSS_SVC)
		*best_score = 0.0;
	else if(param_tmp->solver_type == L2R_L2LOSS_SVR)
		*best_score = INF;
	*best_C = start_C;

	param_tmp->C = start_C;
	while(param_tmp->C <= max_C)
	{
		//Output disabled for running CV at a particular C
		set_print_string_function(&print_null);

		for(i=0; i<nr_fold; i++)
		{
			int j;
			int begin = fold_start[i];
			int end = fold_start[i+1];

			param_tmp->init_sol = prev_w[i];
			struct model *submodel = train(&subprob[i],param_tmp);

			int total_w_size;
			if(submodel->nr_class == 2)
				total_w_size = subprob[i].n;
			else
				total_w_size = subprob[i].n * submodel->nr_class;

			if(prev_w[i] == NULL)
			{
				prev_w[i] = Malloc(double, total_w_size);
				for(j=0; j<total_w_size; j++)
					prev_w[i][j] = submodel->w[j];
			}
			else if(num_unchanged_w >= 0)
			{
				double norm_w_diff = 0;
				for(j=0; j<total_w_size; j++)
				{
					norm_w_diff += (submodel->w[j] - prev_w[i][j])*(submodel->w[j] - prev_w[i][j]);
					prev_w[i][j] = submodel->w[j];
				}
				norm_w_diff = sqrt(norm_w_diff);

				if(norm_w_diff > 1e-15)
					num_unchanged_w = -1;
			}
			else
			{
				for(j=0; j<total_w_size; j++)
					prev_w[i][j] = submodel->w[j];
			}

			for(j=begin; j<end; j++)
				target[perm[j]] = predict(submodel,prob->x[perm[j]]);

			free_and_destroy_model(&submodel);
		}
		set_print_string_function(default_print_string);

		if(param_tmp->solver_type == L2R_LR || param_tmp->solver_type == L2R_L2LOSS_SVC)
		{
			int total_correct = 0;
			for(i=0; i<prob->l; i++)
				if(target[i] == prob->y[i])
					++total_correct;
			double current_rate = (double)total_correct/prob->l;
			if(current_rate > *best_score)
			{
				*best_C = param_tmp->C;
				*best_score = current_rate;
			}

			info("log2c=%7.2f\trate=%g\n",log(param_tmp->C)/log(2.0),100.0*current_rate);
		}
		else if(param_tmp->solver_type == L2R_L2LOSS_SVR)
		{
			double total_error = 0.0;
			for(i=0; i<prob->l; i++)
			{
				double y = prob->y[i];
				double v = target[i];
				total_error += (v-y)*(v-y);
			}
			double current_error = total_error/prob->l;
			if(current_error < *best_score)
			{
				*best_C = param_tmp->C;
				*best_score = current_error;
			}

			info("log2c=%7.2f\tp=%7.2f\tMean squared error=%g\n",log(param_tmp->C)/log(2.0),param_tmp->p,current_error);
		}

		num_unchanged_w++;
		if(num_unchanged_w == 5)
			break;
		param_tmp->C = param_tmp->C*ratio;
	}

	if(param_tmp->C > max_C)
		info("WARNING: maximum C reached.\n");
	free(target);
	for(i=0; i<nr_fold; i++)
		free(prev_w[i]);
	free(prev_w);
}


//
// Interface functions
//
model* train(const problem *prob, const parameter *param)
{
	int i,j;
	int l = prob->l;
	int n = prob->n;
	int w_size = prob->n;
	model *model_ = Malloc(model,1);

	if(prob->bias>=0)
		model_->nr_feature=n-1;
	else
		model_->nr_feature=n;
	model_->param = *param;
	model_->bias = prob->bias;

	if(check_regression_model(model_))
	{
		model_->w = Malloc(double, w_size);

		if(param->init_sol != NULL)
			for(i=0;i<w_size;i++)
				model_->w[i] = param->init_sol[i];
		else
			for(i=0;i<w_size;i++)
				model_->w[i] = 0;

		model_->nr_class = 2;
		model_->label = NULL;
		train_one(prob, param, model_->w, 0, 0);
	}
	else if(check_oneclass_model(model_))
	{
		model_->w = Malloc(double, w_size);
		model_->nr_class = 2;
		model_->label = NULL;
		solve_oneclass_svm(prob, model_->w, &(model_->rho), param->eps, param->nu);
	}
	else
	{
		int nr_class;
		int *label = NULL;
		int *start = NULL;
		int *count = NULL;
		int *perm = Malloc(int,l);

		// group training data of the same class
		group_classes(prob,&nr_class,&label,&start,&count,perm);

		model_->nr_class=nr_class;
		model_->label = Malloc(int,nr_class);
		for(i=0;i<nr_class;i++)
			model_->label[i] = label[i];

		// calculate weighted C
		double *weighted_C = Malloc(double, nr_class);
		for(i=0;i<nr_class;i++)
			weighted_C[i] = param->C;
		for(i=0;i<param->nr_weight;i++)
		{
			for(j=0;j<nr_class;j++)
				if(param->weight_label[i] == label[j])
					break;
			if(j == nr_class)
				fprintf(stderr,"WARNING: class label %d specified in weight is not found\n", param->weight_label[i]);
			else
				weighted_C[j] *= param->weight[i];
		}

		// constructing the subproblem
		feature_node **x = Malloc(feature_node *,l);
		for(i=0;i<l;i++)
			x[i] = prob->x[perm[i]];

		int k;
		problem sub_prob;
		sub_prob.l = l;
		sub_prob.n = n;
		sub_prob.x = Malloc(feature_node *,sub_prob.l);
		sub_prob.y = Malloc(double,sub_prob.l);

		for(k=0; k<sub_prob.l; k++)
			sub_prob.x[k] = x[k];

		// multi-class svm by Crammer and Singer
		if(param->solver_type == MCSVM_CS)
		{
			model_->w=Malloc(double, n*nr_class);
			for(i=0;i<nr_class;i++)
				for(j=start[i];j<start[i]+count[i];j++)
					sub_prob.y[j] = i;
			Solver_MCSVM_CS Solver(&sub_prob, nr_class, weighted_C, param->eps);
			Solver.Solve(model_->w);
		}
    else if(param->solver_type == MCSVM_WW)
    {
			model_->w=Malloc(double, n*nr_class);
			for(i=0;i<nr_class;i++)
				for(j=start[i];j<start[i]+count[i];j++)
					sub_prob.y[j] = i;

      Solver_MCSVM_WW Solver(&sub_prob, nr_class, param->C, param->eps, param->max_iter);


			Solver.Solve(model_->w);


    }
    else if(param->solver_type == MCSVM_WW_Sparse)
    {
      std::cout<<"not supported yet";
      exit(0);
    }
    else if(param->solver_type == MCSVM_WW_Shark)
    {
			model_->w=Malloc(double, n*nr_class);
			for(i=0;i<nr_class;i++)
				for(j=start[i];j<start[i]+count[i];j++)
					sub_prob.y[j] = i;
			Solver_MCSVM_WW_Shark Solver(&sub_prob, nr_class, 4*param->C, param->eps, param->max_iter);

			Solver.Solve(model_->w);

    }
		else
		{
			if(nr_class == 2)
			{
				model_->w=Malloc(double, w_size);

				int e0 = start[0]+count[0];
				k=0;
				for(; k<e0; k++)
					sub_prob.y[k] = +1;
				for(; k<sub_prob.l; k++)
					sub_prob.y[k] = -1;

				if(param->init_sol != NULL)
					for(i=0;i<w_size;i++)
						model_->w[i] = param->init_sol[i];
				else
					for(i=0;i<w_size;i++)
						model_->w[i] = 0;

				train_one(&sub_prob, param, model_->w, weighted_C[0], weighted_C[1]);
			}
			else
			{
				model_->w=Malloc(double, w_size*nr_class);
				double *w=Malloc(double, w_size);
				for(i=0;i<nr_class;i++)
				{
					int si = start[i];
					int ei = si+count[i];

					k=0;
					for(; k<si; k++)
						sub_prob.y[k] = -1;
					for(; k<ei; k++)
						sub_prob.y[k] = +1;
					for(; k<sub_prob.l; k++)
						sub_prob.y[k] = -1;

					if(param->init_sol != NULL)
						for(j=0;j<w_size;j++)
							w[j] = param->init_sol[j*nr_class+i];
					else
						for(j=0;j<w_size;j++)
							w[j] = 0;

					train_one(&sub_prob, param, w, weighted_C[i], param->C);

					for(j=0;j<w_size;j++)
						model_->w[j*nr_class+i] = w[j];
				}
				free(w);
			}

		}

		free(x);
		free(label);
		free(start);
		free(count);
		free(perm);
		free(sub_prob.x);
		free(sub_prob.y);
		free(weighted_C);
	}
	return model_;
}

void cross_validation(const problem *prob, const parameter *param, int nr_fold, double *target)
{
	int i;
	int *fold_start;
	int l = prob->l;
	int *perm = Malloc(int,l);
	if (nr_fold > l)
	{
		nr_fold = l;
		fprintf(stderr,"WARNING: # folds > # data. Will use # folds = # data instead (i.e., leave-one-out cross validation)\n");
	}
	fold_start = Malloc(int,nr_fold+1);
	for(i=0;i<l;i++) perm[i]=i;
	for(i=0;i<l;i++)
	{
		int j = i+rand()%(l-i);
		my_swap(perm[i],perm[j]);
	}
	for(i=0;i<=nr_fold;i++)
		fold_start[i]=i*l/nr_fold;

	for(i=0;i<nr_fold;i++)
	{
		int begin = fold_start[i];
		int end = fold_start[i+1];
		int j,k;
		struct problem subprob;

		subprob.bias = prob->bias;
		subprob.n = prob->n;
		subprob.l = l-(end-begin);
		subprob.x = Malloc(struct feature_node*,subprob.l);
		subprob.y = Malloc(double,subprob.l);

		k=0;
		for(j=0;j<begin;j++)
		{
			subprob.x[k] = prob->x[perm[j]];
			subprob.y[k] = prob->y[perm[j]];
			++k;
		}
		for(j=end;j<l;j++)
		{
			subprob.x[k] = prob->x[perm[j]];
			subprob.y[k] = prob->y[perm[j]];
			++k;
		}
		struct model *submodel = train(&subprob,param);
		for(j=begin;j<end;j++)
			target[perm[j]] = predict(submodel,prob->x[perm[j]]);
		free_and_destroy_model(&submodel);
		free(subprob.x);
		free(subprob.y);
	}
	free(fold_start);
	free(perm);
}


void find_parameters(const problem *prob, const parameter *param, int nr_fold, double start_C, double start_p, double *best_C, double *best_p, double *best_score)
{
	// prepare CV folds

	int i;
	int *fold_start;
	int l = prob->l;
	int *perm = Malloc(int, l);
	struct problem *subprob = Malloc(problem,nr_fold);

	if (nr_fold > l)
	{
		nr_fold = l;
		fprintf(stderr,"WARNING: # folds > # data. Will use # folds = # data instead (i.e., leave-one-out cross validation)\n");
	}
	fold_start = Malloc(int,nr_fold+1);
	for(i=0;i<l;i++) perm[i]=i;
	for(i=0;i<l;i++)
	{
		int j = i+rand()%(l-i);
		my_swap(perm[i],perm[j]);
	}
	for(i=0;i<=nr_fold;i++)
		fold_start[i]=i*l/nr_fold;

	for(i=0;i<nr_fold;i++)
	{
		int begin = fold_start[i];
		int end = fold_start[i+1];
		int j,k;

		subprob[i].bias = prob->bias;
		subprob[i].n = prob->n;
		subprob[i].l = l-(end-begin);
		subprob[i].x = Malloc(struct feature_node*,subprob[i].l);
		subprob[i].y = Malloc(double,subprob[i].l);

		k=0;
		for(j=0;j<begin;j++)
		{
			subprob[i].x[k] = prob->x[perm[j]];
			subprob[i].y[k] = prob->y[perm[j]];
			++k;
		}
		for(j=end;j<l;j++)
		{
			subprob[i].x[k] = prob->x[perm[j]];
			subprob[i].y[k] = prob->y[perm[j]];
			++k;
		}

	}

	struct parameter param_tmp = *param;
	*best_p = -1;
	if(param->solver_type == L2R_LR || param->solver_type == L2R_L2LOSS_SVC)
	{
		if(start_C <= 0)
			start_C = calc_start_C(prob, &param_tmp);
		double max_C = 1024;
		start_C = min(start_C, max_C);
		double best_C_tmp, best_score_tmp;

		find_parameter_C(prob, &param_tmp, start_C, max_C, &best_C_tmp, &best_score_tmp, fold_start, perm, subprob, nr_fold);

		*best_C = best_C_tmp;
		*best_score = best_score_tmp;
	}
	else if(param->solver_type == L2R_L2LOSS_SVR)
	{
		double max_p = calc_max_p(prob, &param_tmp);
		int num_p_steps = 20;
		double max_C = 1048576;
		*best_score = INF;

		i = num_p_steps-1;
		if(start_p > 0)
			i = min((int)(start_p/(max_p/num_p_steps)), i);
		for(; i >= 0; i--)
		{
			param_tmp.p = i*max_p/num_p_steps;
			double start_C_tmp;
			if(start_C <= 0)
				start_C_tmp = calc_start_C(prob, &param_tmp);
			else
				start_C_tmp = start_C;
			start_C_tmp = min(start_C_tmp, max_C);
			double best_C_tmp, best_score_tmp;

			find_parameter_C(prob, &param_tmp, start_C_tmp, max_C, &best_C_tmp, &best_score_tmp, fold_start, perm, subprob, nr_fold);

			if(best_score_tmp < *best_score)
			{
				*best_p = param_tmp.p;
				*best_C = best_C_tmp;
				*best_score = best_score_tmp;
			}
		}
	}

	free(fold_start);
	free(perm);
	for(i=0; i<nr_fold; i++)
	{
		free(subprob[i].x);
		free(subprob[i].y);
	}
	free(subprob);
}

double predict_values(const struct model *model_, const struct feature_node *x, double *dec_values)
{
	int idx;
	int n;
	if(model_->bias>=0)
		n=model_->nr_feature+1;
	else
		n=model_->nr_feature;
	double *w=model_->w;
	int nr_class=model_->nr_class;
	int i;
	int nr_w;
	if(nr_class==2 && model_->param.solver_type != MCSVM_CS)
		nr_w = 1;
	else
		nr_w = nr_class;

	const feature_node *lx=x;
	for(i=0;i<nr_w;i++)
		dec_values[i] = 0;
	for(; (idx=lx->index)!=-1; lx++)
	{
		// the dimension of testing data may exceed that of training
		if(idx<=n)
			for(i=0;i<nr_w;i++)
				dec_values[i] += w[(idx-1)*nr_w+i]*lx->value;
	}
	if(check_oneclass_model(model_))
		dec_values[0] -= model_->rho;

	if(nr_class==2)
	{
		if(check_regression_model(model_))
			return dec_values[0];
		else if(check_oneclass_model(model_))
			return (dec_values[0]>0)?1:-1;
		else
			return (dec_values[0]>0)?model_->label[0]:model_->label[1];
	}
	else
	{
		int dec_max_idx = 0;
		for(i=1;i<nr_class;i++)
		{
			if(dec_values[i] > dec_values[dec_max_idx])
				dec_max_idx = i;
		}
		return model_->label[dec_max_idx];
	}
}

double predict(const model *model_, const feature_node *x)
{
	double *dec_values = Malloc(double, model_->nr_class);
	double label=predict_values(model_, x, dec_values);
	free(dec_values);
	return label;
}

double predict_probability(const struct model *model_, const struct feature_node *x, double* prob_estimates)
{
	if(check_probability_model(model_))
	{
		int i;
		int nr_class=model_->nr_class;
		int nr_w;
		if(nr_class==2)
			nr_w = 1;
		else
			nr_w = nr_class;

		double label=predict_values(model_, x, prob_estimates);
		for(i=0;i<nr_w;i++)
			prob_estimates[i]=1/(1+exp(-prob_estimates[i]));

		if(nr_class==2) // for binary classification
			prob_estimates[1]=1.-prob_estimates[0];
		else
		{
			double sum=0;
			for(i=0; i<nr_class; i++)
				sum+=prob_estimates[i];

			for(i=0; i<nr_class; i++)
				prob_estimates[i]=prob_estimates[i]/sum;
		}

		return label;
	}
	else
		return 0;
}

static const char *solver_type_table[]=
{
	"L2R_LR", "L2R_L2LOSS_SVC_DUAL", "L2R_L2LOSS_SVC", "L2R_L1LOSS_SVC_DUAL", "MCSVM_CS",
	"L1R_L2LOSS_SVC", "L1R_LR", "L2R_LR_DUAL",
	"", "", "",
	"L2R_L2LOSS_SVR", "L2R_L2LOSS_SVR_DUAL", "L2R_L1LOSS_SVR_DUAL",
	"", "", "", "", "", "", "",
	"ONECLASS_SVM", 
  "","","","","","","","",
  "MCSVM_WW", "MCSVM_WW_Shark", "MCSVM_WW_Sparse", NULL
};

int save_model(const char *model_file_name, const struct model *model_)
{
	int i;
	int nr_feature=model_->nr_feature;
	int n;
	const parameter& param = model_->param;

	if(model_->bias>=0)
		n=nr_feature+1;
	else
		n=nr_feature;
	int w_size = n;
	FILE *fp = fopen(model_file_name,"w");
	if(fp==NULL) return -1;

	char *old_locale = setlocale(LC_ALL, NULL);
	if (old_locale)
	{
		old_locale = strdup(old_locale);
	}
	setlocale(LC_ALL, "C");

	int nr_w;
	if(model_->nr_class==2 && model_->param.solver_type != MCSVM_CS)
		nr_w=1;
	else
		nr_w=model_->nr_class;

	fprintf(fp, "solver_type %s\n", solver_type_table[param.solver_type]);
	fprintf(fp, "nr_class %d\n", model_->nr_class);

	if(model_->label)
	{
		fprintf(fp, "label");
		for(i=0; i<model_->nr_class; i++)
			fprintf(fp, " %d", model_->label[i]);
		fprintf(fp, "\n");
	}

	fprintf(fp, "nr_feature %d\n", nr_feature);

	fprintf(fp, "bias %.17g\n", model_->bias);

	if(check_oneclass_model(model_))
		fprintf(fp, "rho %.17g\n", model_->rho);

	fprintf(fp, "w\n");
	for(i=0; i<w_size; i++)
	{
		int j;
		for(j=0; j<nr_w; j++)
			fprintf(fp, "%.17g ", model_->w[i*nr_w+j]);
		fprintf(fp, "\n");
	}

	setlocale(LC_ALL, old_locale);
	free(old_locale);

	if (ferror(fp) != 0 || fclose(fp) != 0) return -1;
	else return 0;
}

//
// FSCANF helps to handle fscanf failures.
// Its do-while block avoids the ambiguity when
// if (...)
//    FSCANF();
// is used
//
#define FSCANF(_stream, _format, _var)do\
{\
	if (fscanf(_stream, _format, _var) != 1)\
	{\
		fprintf(stderr, "ERROR: fscanf failed to read the model\n");\
		EXIT_LOAD_MODEL()\
	}\
}while(0)
// EXIT_LOAD_MODEL should NOT end with a semicolon.
#define EXIT_LOAD_MODEL()\
{\
	setlocale(LC_ALL, old_locale);\
	free(model_->label);\
	free(model_);\
	free(old_locale);\
	return NULL;\
}
struct model *load_model(const char *model_file_name)
{
	FILE *fp = fopen(model_file_name,"r");
	if(fp==NULL) return NULL;

	int i;
	int nr_feature;
	int n;
	int nr_class;
	double bias;
	double rho;
	model *model_ = Malloc(model,1);
	parameter& param = model_->param;
	// parameters for training only won't be assigned, but arrays are assigned as NULL for safety
	param.nr_weight = 0;
	param.weight_label = NULL;
	param.weight = NULL;
	param.init_sol = NULL;

	model_->label = NULL;

	char *old_locale = setlocale(LC_ALL, NULL);
	if (old_locale)
	{
		old_locale = strdup(old_locale);
	}
	setlocale(LC_ALL, "C");

	char cmd[81];
	while(1)
	{
		FSCANF(fp,"%80s",cmd);
		if(strcmp(cmd,"solver_type")==0)
		{
			FSCANF(fp,"%80s",cmd);
			int i;
			for(i=0;solver_type_table[i];i++)
			{
				if(strcmp(solver_type_table[i],cmd)==0)
				{
					param.solver_type=i;
					break;
				}
			}
			if(solver_type_table[i] == NULL)
			{
				fprintf(stderr,"unknown solver type.\n");
				EXIT_LOAD_MODEL()
			}
		}
		else if(strcmp(cmd,"nr_class")==0)
		{
			FSCANF(fp,"%d",&nr_class);
			model_->nr_class=nr_class;
		}
		else if(strcmp(cmd,"nr_feature")==0)
		{
			FSCANF(fp,"%d",&nr_feature);
			model_->nr_feature=nr_feature;
		}
		else if(strcmp(cmd,"bias")==0)
		{
			FSCANF(fp,"%lf",&bias);
			model_->bias=bias;
		}
		else if(strcmp(cmd,"rho")==0)
		{
			FSCANF(fp,"%lf",&rho);
			model_->rho=rho;
		}
		else if(strcmp(cmd,"w")==0)
		{
			break;
		}
		else if(strcmp(cmd,"label")==0)
		{
			int nr_class = model_->nr_class;
			model_->label = Malloc(int,nr_class);
			for(int i=0;i<nr_class;i++)
				FSCANF(fp,"%d",&model_->label[i]);
		}
		else
		{
			fprintf(stderr,"unknown text in model file: [%s]\n",cmd);
			EXIT_LOAD_MODEL()
		}
	}

	nr_feature=model_->nr_feature;
	if(model_->bias>=0)
		n=nr_feature+1;
	else
		n=nr_feature;
	int w_size = n;
	int nr_w;
	if(nr_class==2 && param.solver_type != MCSVM_CS)
		nr_w = 1;
	else
		nr_w = nr_class;

	model_->w=Malloc(double, w_size*nr_w);
	for(i=0; i<w_size; i++)
	{
		int j;
		for(j=0; j<nr_w; j++)
			FSCANF(fp, "%lf ", &model_->w[i*nr_w+j]);
	}

	setlocale(LC_ALL, old_locale);
	free(old_locale);

	if (ferror(fp) != 0 || fclose(fp) != 0) return NULL;

	return model_;
}

int get_nr_feature(const model *model_)
{
	return model_->nr_feature;
}

int get_nr_class(const model *model_)
{
	return model_->nr_class;
}

void get_labels(const model *model_, int* label)
{
	if (model_->label != NULL)
		for(int i=0;i<model_->nr_class;i++)
			label[i] = model_->label[i];
}

// use inline here for better performance (around 20% faster than the non-inline one)
static inline double get_w_value(const struct model *model_, int idx, int label_idx)
{
	int nr_class = model_->nr_class;
	int solver_type = model_->param.solver_type;
	const double *w = model_->w;

	if(idx < 0 || idx > model_->nr_feature)
		return 0;
	if(check_regression_model(model_) || check_oneclass_model(model_))
		return w[idx];
	else
	{
		if(label_idx < 0 || label_idx >= nr_class)
			return 0;
		if(nr_class == 2 && solver_type != MCSVM_CS)
		{
			if(label_idx == 0)
				return w[idx];
			else
				return -w[idx];
		}
		else
			return w[idx*nr_class+label_idx];
	}
}

// feat_idx: starting from 1 to nr_feature
// label_idx: starting from 0 to nr_class-1 for classification models;
//            for regression and one-class SVM models, label_idx is
//            ignored.
double get_decfun_coef(const struct model *model_, int feat_idx, int label_idx)
{
	if(feat_idx > model_->nr_feature)
		return 0;
	return get_w_value(model_, feat_idx-1, label_idx);
}

double get_decfun_bias(const struct model *model_, int label_idx)
{
	if(check_oneclass_model(model_))
	{
		fprintf(stderr, "ERROR: get_decfun_bias can not be called for a one-class SVM model\n");
		return 0;
	}
	int bias_idx = model_->nr_feature;
	double bias = model_->bias;
	if(bias <= 0)
		return 0;
	else
		return bias*get_w_value(model_, bias_idx, label_idx);
}

double get_decfun_rho(const struct model *model_)
{
	if(check_oneclass_model(model_))
		return model_->rho;
	else
	{
		fprintf(stderr, "ERROR: get_decfun_rho can be called only for a one-class SVM model\n");
		return 0;
	}
}

void free_model_content(struct model *model_ptr)
{
	if(model_ptr->w != NULL)
		free(model_ptr->w);
	if(model_ptr->label != NULL)
		free(model_ptr->label);
}

void free_and_destroy_model(struct model **model_ptr_ptr)
{
	struct model *model_ptr = *model_ptr_ptr;
	if(model_ptr != NULL)
	{
		free_model_content(model_ptr);
		free(model_ptr);
	}
}

void destroy_param(parameter* param)
{
	if(param->weight_label != NULL)
		free(param->weight_label);
	if(param->weight != NULL)
		free(param->weight);
	if(param->init_sol != NULL)
		free(param->init_sol);
}

const char *check_parameter(const problem *prob, const parameter *param)
{
	if(param->eps <= 0)
		return "eps <= 0";

	if(param->C <= 0)
		return "C <= 0";

	if(param->p < 0)
		return "p < 0";

	if(prob->bias >= 0 && param->solver_type == ONECLASS_SVM)
		return "prob->bias >=0, but this is ignored in ONECLASS_SVM";

	if(param->regularize_bias == 0)
	{
		if(prob->bias != 1.0)
			return "To not regularize bias, must specify -B 1 along with -R";
		if(param->solver_type != L2R_LR
			&& param->solver_type != L2R_L2LOSS_SVC
			&& param->solver_type != L1R_L2LOSS_SVC
			&& param->solver_type != L1R_LR
			&& param->solver_type != L2R_L2LOSS_SVR)
			return "-R option supported only for solver L2R_LR, L2R_L2LOSS_SVC, L1R_L2LOSS_SVC, L1R_LR, and L2R_L2LOSS_SVR";
	}

	if(param->solver_type != L2R_LR
		&& param->solver_type != L2R_L2LOSS_SVC_DUAL
		&& param->solver_type != L2R_L2LOSS_SVC
		&& param->solver_type != L2R_L1LOSS_SVC_DUAL
		&& param->solver_type != MCSVM_CS
    && param->solver_type != MCSVM_WW
    && param->solver_type != MCSVM_WW_Shark
    && param->solver_type != MCSVM_WW_Sparse
		&& param->solver_type != L1R_L2LOSS_SVC
		&& param->solver_type != L1R_LR
		&& param->solver_type != L2R_LR_DUAL
		&& param->solver_type != L2R_L2LOSS_SVR
		&& param->solver_type != L2R_L2LOSS_SVR_DUAL
		&& param->solver_type != L2R_L1LOSS_SVR_DUAL
		&& param->solver_type != ONECLASS_SVM)
		return "unknown solver type";

	if(param->init_sol != NULL
		&& param->solver_type != L2R_LR
		&& param->solver_type != L2R_L2LOSS_SVC
		&& param->solver_type != L2R_L2LOSS_SVR)
		return "Initial-solution specification supported only for solvers L2R_LR, L2R_L2LOSS_SVC, and L2R_L2LOSS_SVR";

	return NULL;
}

int check_probability_model(const struct model *model_)
{
	return (model_->param.solver_type==L2R_LR ||
			model_->param.solver_type==L2R_LR_DUAL ||
			model_->param.solver_type==L1R_LR);
}

int check_regression_model(const struct model *model_)
{
	return (model_->param.solver_type==L2R_L2LOSS_SVR ||
			model_->param.solver_type==L2R_L1LOSS_SVR_DUAL ||
			model_->param.solver_type==L2R_L2LOSS_SVR_DUAL);
}

int check_oneclass_model(const struct model *model_)
{
	return model_->param.solver_type == ONECLASS_SVM;
}

void set_print_string_function(void (*print_func)(const char*))
{
	if (print_func == NULL)
		liblinear_print_string = &print_string_stdout;
	else
		liblinear_print_string = print_func;
}


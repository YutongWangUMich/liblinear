This is the repository that accompanies the manuscript ["An exact solver for the Weston-Watkins SVM subproblem"](https://arxiv.org/abs/2102.05640).

# Basic usage

To train the WW-SVM on `mnist.scale` -- downloadable from [LIBSVM Data: Classification (Multi-class)
](https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass.html) -- run the following in console:

```
make
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/mnist.scale.bz2
bunzip2 mnist.scale.bz2
./train -s 30 -c 1 -t 64 mnist.scale
```

The `-s 30` flag is for choosing the Walrus solver for the linear WW-SVM.
Alternatively, the `-s 31` flag is for choosing the [Shark](https://github.com/Shark-ML/Shark/) solver for the linear WW-SVM.
The `-c 1` flag is for setting the regularizer to `1`.
The `-t 64` sets the max outer iterations to 64.

To run the trained model on test set:
```
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/mnist.scale.t.bz2
bunzip2 mnist.scale.t.bz2
./predict mnist.scale.t mnist.scale.model output
```




# Stopping criterion

The default stopping criterion is when the duality gap decays by a factor of 0.0009 relative to right after the first iteration. See the manuscript for more details.

This can be changed by modifying 
```
#define STOPPING_CRITERION 0.0009
```
to a different value.

# Reproducing the figures from the manuscript

After cloning the repository run the following in the console

```
chmod +x reproduce_figures.sh
./reproduce_figures.sh
```

This script performs the following:
1. Download all the datasets 
2. Run the Walrus and Shark solver on each dataset over the hyperparameter grid of size 10 `[2^(-6), 2^(-5),...,2^(3)]`.

After the above script finishes, make sure the following Python packages are installed:

```
pandas
numpy
matplotlib
seaborn
re
```

Finally, go to the Jupyter notebook `experiments/analyze.ipynb` and hit run all.


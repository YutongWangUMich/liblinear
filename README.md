This is the repository that accompanies the manuscript:

# Basic usage

To run the Walrus solver for the WW-SVM:
`./train -s 30 -c REGULARIZER -t MAX_ITERATION data/mnist.scale`


For example, to train the WW-SVM on `mnist.scale` -- downloadable from [LIBSVM Data: Classification (Multi-class)
](https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass.html)
```
./train -s 30 -c 1 -t 64 data/mnist.scale
./predict data/mnist.scale.t mnist.scale.model output
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

This script will download all the datasets. After the above script finishes, go to the Jupyter notebook `experiments/analyze.ipynb` and hit run all.


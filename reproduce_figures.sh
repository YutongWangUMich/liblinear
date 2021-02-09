mkdir data
mkdir accuracies
mkdir accuracies/Ours
mkdir accuracies/Shark
mkdir results
mkdir results/Ours
mkdir results/Shark
mkdir figures
cd experiments
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/aloi.scale.bz2
bunzip2 aloi.scale.bz2
python aloi_train_test_split.py
cd ../data
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/dna.scale
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/dna.scale.t
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/letter.scale
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/letter.scale.t
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/mnist.scale.bz2
bunzip2 mnist.scale.bz2
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/mnist.scale.t.bz2
bunzip2 mnist.scale.t.bz2
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/news20.scale.bz2
bunzip2 news20.scale.bz2
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/news20.t.scale.bz2
bunzip2 news20.t.scale.bz2
mv news20.t.scale news20.scale.t
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/rcv1_train.multiclass.bz2
bunzip2 rcv1_train.multiclass.bz2
mv rcv1_train.multiclass rcv1
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/rcv1_test.multiclass.bz2
bunzip2 rcv1_test.multiclass.bz2
mv rcv1_test.multiclass rcv1.t
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/satimage.scale
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/satimage.scale.t
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/sector/sector.scale.bz2
bunzip2 sector.scale.bz2
wget https://www.csie.ntu.edu.tw/~cjlin/libsvmtools/datasets/multiclass/sector/sector.t.scale.bz2
bunzip2 sector.t.scale.bz2
mv sector.t.scale sector.scale.t
cd experiments/run_multiple.py

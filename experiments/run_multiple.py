import pandas as pd
ds_metadata = pd.read_csv('dataset_metadata.csv')
# +----+----------------+--------------+----------------+---------------+--------------+
# |    | ds_name        |   nr_classes |   nr_instances |   nr_features | short_name   |
# |----+----------------+--------------+----------------+---------------+--------------|
# |  0 | dna.scale      |            3 |           2000 |           180 | dna          |
# |  1 | satimage.scale |            6 |           4435 |            36 | satimage     |
# |  2 | mnist.scale    |           10 |          60000 |           780 | mnist        |
# |  3 | news20.scale   |           20 |          15935 |         62061 | news20       |
# |  4 | letter.scale   |           26 |          15000 |            16 | letter       |
# |  5 | rcv1           |           53 |          15564 |         47236 | rcv1         |
# |  6 | sector.scale   |          105 |           6412 |         55197 | sector       |
# |  7 | aloi.scale     |         1000 |         108000 |           128 | aloi         |
# +----+----------------+--------------+----------------+---------------+--------------+

# value = input("This takes long. Are you sure you want to run? Type YES:\n")
# if(value != "YES"): 
#     quit()




# all data sets
datasets = range(8) 
# all hyperparameters
hyperparameters = [2**(i-6) for i in range(10)] 


import os

if "original_dir" not in locals():
    original_dir = os.getcwd()
os.chdir(original_dir)
os.chdir('..')
os.getcwd()






def run_experiment(dataset_name, regularizer, num_iterations, variant):
    expname = get_exp_name(dataset_name, regularizer)
    if variant == "Ours":
        FILENAME = 'results/Ours/' + expname
        COMMAND = './train -s 30 -c ' + str(regularizer) + ' -t ' + str(num_iterations) + ' data/' + dataset_name + ' >> '+ FILENAME
    elif variant == "Shark":
        FILENAME = 'results/Shark/' + expname
        COMMAND = './train -s 31 -c ' + str(regularizer) + ' -t ' + str(num_iterations) + ' data/' + dataset_name + ' >> '+ FILENAME
    else:
        print("Invalid variant")
        return
    if os.path.isfile(FILENAME):
        os.system('rm ' + FILENAME)
    os.system(COMMAND)
    
    FILENAME = 'accuracies/'+ variant +'/' + expname
    if os.path.isfile(FILENAME):
        os.system('rm ' + FILENAME)
    TEST_COMMAND = './predict data/' + dataset_name + '.t ' + dataset_name + '.model output' + ' >> ' + FILENAME
    os.system(TEST_COMMAND)


    



    

    
import math

def get_exp_name(ds, reg):
    return ds+'_'+ str(int(math.log2(reg)))+'.csv'


    
MAX_ITERS = 100000

for i in datasets:
    for C in hyperparameters:
        ds = ds_metadata['ds_name'][i]
        print(ds)
        print(C)
        print("Running Walrus")
        run_experiment(ds, C, MAX_ITERS, "Ours")
        print("Running Shark")
        run_experiment(ds, C, MAX_ITERS, "Shark")

import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import os
import re
import math
import pandas as pd
good_params = pd.read_pickle("good_params.pkl")

if "original_dir" not in locals():
    original_dir = os.getcwd()
os.chdir(original_dir)
os.chdir('..')
os.getcwd()



iters = {'usps':2048,\
         'satimage.scale':1024,\
         'dna.scale':1024,\
         'letter.scale':1024,\
         'pendigits':8192,\
         'news20.scale':1024,\
         'sector.scale':1024,\
         'mnist.scale':1024,\
         'cifar10':1024,\
         'toy':1024,\
         'rcv1':1024,\
         'aloi.scale':1024\
       }




def run_experiment(dataset_name, regularizer, num_iterations, variant):
    FILENAME = 'temp'
    if variant == "Ours":
        TRAIN_COMMAND = './train -s 30 -c ' + str(regularizer) + ' -t ' + str(num_iterations) + ' data/' + dataset_name + ' >> '+ FILENAME
    elif variant == "Shark":
        TRAIN_COMMAND = './train -s 31 -c ' + str(regularizer) + ' -t ' + str(num_iterations) + ' data/' + dataset_name + ' >> '+ FILENAME
    else:
#         print("Invalid variant")
        return
    if os.path.isfile(FILENAME):
        os.system('rm ' + FILENAME)
    os.system(TRAIN_COMMAND)

    TEST_COMMAND = './predict data/' + dataset_name + '.t ' + dataset_name + '.model output'
#     print(variant)
    os.system(TEST_COMMAND)
    
    
def get_exp_name(ds, reg):
    return ds+'_'+ str(int(math.log2(reg)))+'.csv'





    
    
for index,row in good_params.iterrows():
    ds = row['ds_name']
    C = row['regularizer']
    run_experiment(ds, C, iters[ds], "Ours")
    run_experiment(ds, C, iters[ds], "Shark")

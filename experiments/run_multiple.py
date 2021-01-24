import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import os
import re
import math



if "original_dir" not in locals():
    original_dir = os.getcwd()
os.chdir(original_dir)
os.chdir('..')
os.getcwd()




dataset_names = {0:'usps',\
                 1:'satimage.scale',\
                 2:'dna.scale',\
                 3:'letter.scale',\
                 4:'pendigits',\
                 5:'news20.scale',\
                 6:'sector.scale',\
                 7:'mnist.scale',\
                 8:'cifar10',\
                 9:'toy',\
                 10:'rcv1',\
                 11:'aloi.scale'\
                }

nr_classes = {'usps':10,\
              'satimage.scale':6,\
              'dna.scale':3,\
              'letter.scale':26,\
              'pendigits':10,\
              'news20.scale':20,\
              'sector.scale':105,\
              'mnist.scale':10,\
              'cifar10':10,\
              'toy':3,\
              'rcv1':53,\
              'aloi.scale':1000\
             }

# as powers of ten
regs = {'usps':.1,\
        'satimage.scale':1,\
        'dna.scale':1,\
        'letter.scale':1,\
        'pendigits':.01,\
        'news20.scale':1,\
        'sector.scale':1,\
        'mnist.scale':1,\
        'cifar10':1,\
        'toy':1,\
        'rcv1':1,\
        'aloi.scale':1\
       }


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




run_these_datasets = [2,1,7,5,3,10,6,11]
# run_these_datasets = [2,1,7,5,3]
run_these_hyperparams = [100,10,1,0.1,0.01]





def run_experiment(dataset_name, regularizer, num_iterations, variant):
    if variant == "Ours":
        FILENAME = 'results/Ours/' + get_exp_name(dataset_name, regularizer)
        COMMAND = './train -s 30 -c ' + str(regularizer) + ' -t ' + str(num_iterations) + ' data/' + dataset_name + ' >> '+ FILENAME
    elif variant == "Shark":
        FILENAME = 'results/Shark/' + get_exp_name(dataset_name, regularizer)
        COMMAND = './train -s 31 -c ' + str(regularizer) + ' -t ' + str(num_iterations) + ' data/' + dataset_name + ' >> '+ FILENAME
    else:
        print("Invalid variant")
        return
    if os.path.isfile(FILENAME):
        os.system('rm ' + FILENAME)
    os.system(COMMAND)
    
    
def get_exp_name(ds, reg):
    return ds+'_'+ str(int(math.log10(reg)))+'.csv'





    
    
    
    
    
    
    
for i in run_these_datasets:
    for C in run_these_hyperparams:
        print(dataset_names[i])
        print(C)
        ds = dataset_names[i]
        run_experiment(ds, C*regs[ds], iters[ds], "Ours")
        run_experiment(ds, C*regs[ds], iters[ds], "Shark")

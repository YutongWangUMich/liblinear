import re

import numpy as np



def get_labels(lines):
    return np.array(list(map(lambda x: int(x.split(" ")[0]), lines)))


f = open("aloi.scale", "r")
lines = f.readlines()

y = get_labels(lines)

cts = [0 for i in range(1000)]
train = []
test = []

for i, lab in enumerate(y):
    if(cts[lab]< 81):
        train.append(lines[i])
    else:
        test.append(lines[i])
    cts[lab] = cts[lab] + 1

y_train = get_labels(train)

y_test = get_labels(test)

f=open('../data/aloi.scale','w')
train_s=''.join(train)
f.write(train_s)
f.close()


f=open('../data/aloi.scale.t','w')
test_s=''.join(test)
f.write(test_s)
f.close()

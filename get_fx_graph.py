import torch
import torchvision
from torch.autograd import Variable
from torchvision import datasets, transforms
from torch.utils.data import DataLoader
# import cv2
from nets.resnet50 import Bottleneck, ResNet
from tqdm import tqdm
import torch.nn as nn
import torch.fx as fx
from torchsummary import summary
from pprint import pprint
from collections import OrderedDict
import pandas as pd
from functools import reduce
from operator import mul


# # 获取每层输出形状和计算数据量
# def print_layer_data_transfer(model, input_size, batch_size=1):
#     def register_hook(module, name):
#         def hook(name, module, input, output):
#             class_name = str(module.__class__).split(".")[-1].split("'")[0]
#             module_idx = len(summary)

#             m_key = "%s-%i" % (class_name, module_idx + 1)
#             summary[m_key] = OrderedDict()
#             summary[m_key]["name"] = name
#             summary[m_key]["output_shape"] = list(output.size())
            
#             # 计算每层输出的数据量（以字节为单位）
#             output_size = torch.tensor(output.size()).prod().item()
#             dtype_size = output.element_size()
#             summary[m_key]["output_size (bytes)"] = output_size * dtype_size

#         if not isinstance(module, nn.Sequential) and not isinstance(module, nn.ModuleList) and not (module == model):
#             hooks.append(module.register_forward_hook(hook))

#     summary = OrderedDict()
#     hooks = []

# # 注册钩子
#     for name, module in model.named_modules():
#         register_hook(module, name)

#     # 创建一个dummy输入
#     x = torch.rand(batch_size, *input_size)
#     with torch.no_grad():
#         model(x)

#     # 移除钩子
#     for h in hooks:
#         h.remove()

#     return summary



# 提取GraphModule信息并转换为DataFrame
def graph_module_to_dataframe(graph_module, input_size, batch_size=1):
    # # 前向传播过程中记录每个节点的输出形状
    input_shapes = {}
    output_shapes = {}
    param_shapes = {}
    param_num = {}
    dtype_size = {}
    layer_types = {}
    input_output_shapes = {}

    count_dict = {}
    def update_count(key):
        if key in count_dict:
            count_dict[key] += 1
        else:
            count_dict[key] = 0
        return count_dict[key]

    def record_input_output_shapes(module, input, output):
        module_name = ''
        # print("--------------------------------------------------")
        for name, mod in model.named_modules():
            # print(name, mod)
            
            if mod is module:
                module_name = name
                index = update_count(module_name)
                if index > 0:
                    module_name = name.replace('.', '_') + '_' + str(index)
                else:
                    module_name = name.replace('.', '_')
                # print(module_name)
                break
        
        input_shapes[module_name] = input[0].shape if isinstance(input, tuple) else input.shape
        output_shapes[module_name] = output.shape
        # 记录参数形状
        param_shapes[module_name] = {name: p.shape for name, p in module.named_parameters()}
        param_size = 0
        for _, p in module.named_parameters():
            param_size += torch.prod(torch.tensor(p.shape)).item()
        param_num[module_name] = int(param_size)
        dtype_size[module_name] = [output.element_size()]
        layer_types[module_name] = type(module).__name__

        input_output_shapes[module_name] = {
            'input_shape': input_shapes[module_name],
            'output_shape': output_shapes[module_name],
            'Param #': param_num[module_name],
            'param_shape': param_shapes[module_name],
            'dtype_size': dtype_size[module_name],
            'layer_type': layer_types[module_name]
        }

    # 注册钩子
    hooks = []
    for name, module in model.named_modules():
        if not isinstance(module, torch.nn.Sequential):
            hooks.append(module.register_forward_hook(record_input_output_shapes))

    # 前向传播,  创建一个dummy输入
    x = torch.rand(batch_size, *input_size)
    with torch.no_grad():
        model(x)

    # 移除钩子
    for hook in hooks:
        hook.remove()


    node_info = {
        "name": [],
        "args": [],
        "input_#": [],
        "output_#": [],
        "Param_#": [],
        "dp_index": 0,
        "pp_index": 0,
        "tp_index": 0,  # currently not used

        "opcode": [],
        "target": [],
        "layer_type": [],
        "input_shape": [],
        "output_shape": [],
        "param_shape": [],
        "kwargs": [],
        "dtype_size": []
    }

    for node in graph_module.graph.nodes:
        node_info["name"].append(node.name)
        node_info["opcode"].append(node.op)
        node_info["target"].append(node.target)
        node_info["args"].append(node.args)
        node_info["kwargs"].append(node.kwargs)

        # 获取输入和输出形状以及参数形状
        # print(node.name)
        if node.op == 'call_module' or node.name in input_output_shapes:
            node_info["input_shape"].append(list(input_output_shapes[node.name]['input_shape']))
            node_info["output_shape"].append(list(input_output_shapes[node.name]['output_shape']))
            node_info["param_shape"].append(input_output_shapes[node.name]['param_shape'])
            node_info["dtype_size"].append(input_output_shapes[node.name]['dtype_size'])
            node_info["input_#"].append(reduce(mul, list(input_output_shapes[node.name]['input_shape'])))
            node_info["output_#"].append(reduce(mul, list(input_output_shapes[node.name]['output_shape'])))
            node_info["Param_#"].append(input_output_shapes[node.name]['Param #'])
            node_info["layer_type"].append(input_output_shapes[node.name]['layer_type'])
        else:
            if len(node_info["output_shape"]) > 0:     # 用于处理原本为None的情况：给前后层连起来
                node_info["input_shape"].append(node_info["output_shape"][-1])
                node_info["output_shape"].append(node_info["output_shape"][-1])
                node_info["param_shape"].append(0)
                node_info["dtype_size"].append(node_info["dtype_size"][-1])
                node_info["input_#"].append(reduce(mul, node_info["output_shape"][-1]))
                node_info["output_#"].append(reduce(mul, node_info["output_shape"][-1]))
                node_info["Param_#"].append(0)
                node_info["layer_type"].append(None)    # dont care
            else:
                node_info["input_shape"].append(None)
                node_info["output_shape"].append(None)
                node_info["param_shape"].append(None)
                node_info["dtype_size"].append(None)
                node_info["input_#"].append(0)
                node_info["output_#"].append(0)
                node_info["Param_#"].append(0)
                node_info["layer_type"].append(None)

    df = pd.DataFrame(node_info)
    return df


def can_partition(nums, n, max_sum):
    current_sum = 0
    count = 1
    for num in nums:
        if current_sum + num > max_sum:
            count += 1
            current_sum = num
            if count > n:
                return False
        else:
            current_sum += num
    return True

def partition_list(nums, n):
    if n == 0:
        return None
    if len(nums) <= n:
        return [[num] for num in nums] + [[] for _ in range(n - len(nums))]

    low, high = max(nums), sum(nums)
    while low < high:
        mid = (low + high) // 2
        if can_partition(nums, n, mid):
            high = mid
        else:
            low = mid + 1

    partitions = []
    current_sum = 0
    current_partition = []
    for num in nums:
        if current_sum + num > low:
            partitions.append(current_partition)
            current_partition = [num]
            current_sum = num
        else:
            current_partition.append(num)
            current_sum += num

    partitions.append(current_partition)
    
    while len(partitions) < n:
        partitions.append([])

    return partitions

def update_dataframe(df, df_title='Param_#', n=8):
    nums = df[df_title].tolist()  # Updated column name here
    partitions = partition_list(nums, n)

    # Initialize columns
    df['avg_mem_grp_idx'] = 0
    df['avg_mem_grp_size'] = 0
    
    # Fill the DataFrame with group assignments and sums
    group_sums = [sum(part) for part in partitions]
    current_group = 0
    current_sum = 0

    for i, num in enumerate(nums):
        if current_sum + num > group_sums[current_group]:
            current_group += 1
            current_sum = 0
        df.at[i, 'avg_mem_grp_idx'] = current_group + 1
        df.at[i, 'avg_mem_grp_size'] = group_sums[current_group]
        current_sum += num

    return df

# 支持的模式： 'avg_mem' 和 'min_comm'
def model_pp_cut(df, stages=8, cut_mode='avg_mem'):
    if stages < 1:
        stages = 1
    if cut_mode == 'avg_mem':
        update_dataframe(df, 'Param_#', stages)
        
    elif cut_mode == 'min_comm':
        pass
    else:
        print("Error: In function 'model_pp_cut()': cut_mode unrecognized! ")


if __name__ == '__main__':
    # 设置 Pandas 显示选项，不折叠输出
    pd.set_option('display.max_rows', None)
    pd.set_option('display.max_columns', None)
    pd.set_option('display.width', None)
    pd.set_option('display.max_colwidth', None)


    model = ResNet(Bottleneck, [3, 4, 6, 3], num_classes=10)
    # 使用FX跟踪模型
    tracer = fx.Tracer()
    graph = tracer.trace(model)                 # 创建计算图
    # print(graph)

    graph_module = fx.GraphModule(model, graph) # 创建GraphModule
    traced = fx.symbolic_trace(model)           # 创建symbolic的计算图
    traced_df = graph_module_to_dataframe(traced, input_size=(1, 28, 28), batch_size=10)
    print(traced_df)
    traced_df.to_csv('graph_module_info.csv', index=False)

    # 使用torchsummary来显示每层的计算量和参数传递量
    # print("\nsummary()")
    # summary(model=model, input_size=(1, 28, 28), batch_size=1, device='cpu')
    # print("\nsummary end")

    # input_size = (1, 28, 28)
    # layer_data = print_layer_data_transfer(model, input_size, batch_size=1)


    # print("\nprint(df)")
    # df = pd.DataFrame(layer_data).T
    # print(df)
    
    # 模型使用pp切分
    model_pp_cut(traced_df, stages=8, cut_mode='avg_mem')
    print(traced_df)

    


    

import ezdxf
import json
import sys                  # 用于获取命令行参数（比如输入 DXF 路径、输出 JSON 路径），控制程序的命令行运行逻辑
import os                   # 检查文件是否存在，处理文件路径的基础有效性验证

def parse_dxf(file_path):
    if not os.path.exists(file_path):
        print(f"Error: File not found {file_path}")
        return {}

    try:
        doc = ezdxf.readfile(file_path)
    except IOError:                                 # 文件不是DXF格式、文件无法读取
        print(f"Not a DXF file or a generic I/O error.")
        return {}
    except ezdxf.DXFStructureError:                 # DXF文件损坏、格式无效
        print(f"Invalid or corrupted DXF file.")
        return {}

    # 创建存储数据的字典
    data = {
        "holes": [],                                # 存储圆形（孔）的信息：圆心、半径
        "contours": []                              # 存储多段线（轮廓）的信息：顶点坐标列表
    }

    # 遍历 DXF中的所有圆形
    if doc.modelspace():
        msp = doc.modelspace()                                  # 获取DXF模型空间（几何实体主要存在于此）
        hole_id = 1
        temp_holes = []                                         # 临时存储孔数据
        for entity in doc.modelspace().query('CIRCLE'):         # 筛选所有“圆”实体
            center = entity.dxf.center                          # 获取圆心坐标（Point对象，含x/y/z属性）
            radius = entity.dxf.radius                          # 获取圆的半径
            temp_holes.append({
                'id': hole_id,
                'center': [center.x, center.y],
                'radius': radius
            })
            hole_id += 1

        # 排序规则：先按 X轴升序，X相同按 Y轴升序
        temp_holes.sort(key=lambda h: (-round(h['center'][1], 1), round(h['center'][0])))
        for idx, hole in enumerate(temp_holes):                 # 重新分配 id（保证排序后编号连续）
            hole['id'] = idx + 1                                # 从 1开始重新编号
        data['holes'] = temp_holes                              # 将排序后的孔存入 data

        for entity in msp.query('LWPOLYLINE'):                  # 提取轻量级多段线
            points = []
            # 使用 ezdxf上下文管理器获取 xy坐标，忽略 z轴和凸度
            try:
                with entity.points("xy") as vertices:
                    points = [[p[0], p[1]] for p in vertices]
            except Exception:
                continue                                        # 提取失败则跳过
            if len(points) < 2: continue                        # 至少2个点才构成线，跳过无效多段线

            # 处理闭合逻辑：如果属性是闭合的，且首尾点不重合，手动补上起点
            if entity.closed:
                if points[0] != points[-1]:
                    points.append(points[0])
            elif points[0] == points[-1]:
                pass                                            # 非闭合但首尾重合，不处理
            data['contours'].append(points)                     # 存入轮廓列表

        for entity in msp.query('POLYLINE'):                    # 处理2D多段线（传统多段线类型）
            if entity.is_2d_polyline:                           # 只处理2D多段线
                points = []
                # 遍历顶点提取 XY坐标
                for v in entity.vertices:
                    points.append([v.dxf.location.x, v.dxf.location.y])
                if len(points) < 2: continue
                # 处理闭合
                if entity.is_closed:
                    if points[0] != points[-1]:
                        points.append(points[0])
                data['contours'].append(points)

        if len(data['contours']) == 0:                          # 若无多段线轮廓，用直线拼接矩形
            lines = []
            for entity in msp.query('LINE'):                    # 提取所有直线
                start = entity.dxf.start                        # 直线起点
                end = entity.dxf.end                            # 直线终点
                lines.append([[start.x, start.y], [end.x, end.y]])

            if len(lines) >= 4:                                 # 至少4条直线才可能构成矩形
                min_x = min(min(l[0][0], l[1][0]) for l in lines)
                max_x = max(max(l[0][0], l[1][0]) for l in lines)
                min_y = min(min(l[0][1], l[1][1]) for l in lines)
                max_y = max(max(l[0][1], l[1][1]) for l in lines)
                width = max_x - min_x
                height = max_y - min_y
                if width > 0 and height > 0:
                    # 构造闭合矩形轮廓
                    rect_contour = [
                        [min_x, min_y],
                        [max_x, min_y],
                        [max_x, max_y],
                        [min_x, max_y],
                        [min_x, min_y]
                    ]
                    data['contours'].append(rect_contour)

    return data

def save_to_json(dxf_path, output_path):
    data = parse_dxf(dxf_path)          # 调用parse_dxf获取数据
    with open(output_path, 'w') as f:
        json.dump(data, f, indent=4)    # 将数据写入JSON文件，indent=4（缩进参数，可选）让格式更易读

if __name__ == '__main__':
    # sys.argv是 Python 的sys模块提供的字符串列表，它会自动捕获用户在命令行中输入的所有参数
    # 当在命令行执行 Python 脚本时，操作系统会将输入的所有 “以空格分隔的部分” 按顺序存入sys.argv列表中
    if len(sys.argv) < 3:               # 检查命令行参数数量：脚本名+输入DXF路径+输出JSON路径，至少3个参数
        print("Usage: python import_py_test.py <input_dxf_path> <output_json_path>")
        sys.exit(1)                     # 参数不足则打印使用说明并退出

    # 第一个元素（sys.argv[0]）：永远是脚本的文件名（包括路径）
    input_dxf = sys.argv[1]             # 第二个参数是 DXF 路径
    output_json = sys.argv[2]           # 第三个参数是 JSON 保存路径

    save_to_json(input_dxf, output_json)

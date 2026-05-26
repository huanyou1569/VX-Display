"""
指令编解码 —— STM32 VX-Display 蓝牙协议。

文本指令格式: :<CMD> [参数]\n
- :LOAD <文件名>    加载TF卡文件
- :STATUS           查询设备状态
- :LIST             列出TF卡上的.bin文件
- :DATA <字节数>    进入二进制传输模式，后续跟 <字节数> 字节的原始数据

响应格式: 纯文本行，成功回 "OK"，失败回 "ERR:<描述>"
周期性上报: "RPM:<n> DLY:<n> F:<0|1> S:<0|1>"
"""

from typing import Any


def cmd_load(filename: str) -> bytes:
    """加载TF卡中的指定文件。filename 不含路径前缀，如 "chiken.bin" """
    return f":LOAD {filename}\n".encode("ascii")


def cmd_status() -> bytes:
    """查询设备当前状态（转速、延时、稳速标志等）。"""
    return b":STATUS\n"


def cmd_list() -> bytes:
    """列出TF卡根目录下所有 .bin 文件。"""
    return b":LIST\n"


def cmd_data(size: int) -> bytes:
    """通知设备即将发送 size 字节的体素二进制数据。"""
    return f":DATA {size}\n".encode("ascii")


def cmd_save(filename: str, size: int) -> bytes:
    """通知设备接收文件并写入TF卡 (不加载)。"""
    return f":SAVE {filename} {size}\n".encode("ascii")


# ---------------------------------------------------------------------------
# 响应解析
# ---------------------------------------------------------------------------

def is_ok(response: str) -> bool:
    """响应是否为成功标志。"""
    return response.strip().upper() == "OK"


def is_err(response: str) -> bool:
    """响应是否为错误标志。"""
    return response.strip().upper().startswith("ERR")


def parse_status(line: str) -> dict[str, Any]:
    """解析周期性状态上报行。

    示例输入: "RPM:313 DLY:3834 F:1 S:0"
    返回: {"RPM": 313, "DLY": 3834, "F": 1, "S": 0}
    """
    result: dict[str, Any] = {}
    for token in line.strip().split():
        if ":" in token:
            key, val = token.split(":", 1)
            try:
                result[key.upper()] = int(val)
            except ValueError:
                result[key.upper()] = val
    return result


def extract_ok(text: str) -> str | None:
    """从混合文本中提取 OK 或 ERR 行。"""
    for line in text.strip().splitlines():
        s = line.strip().upper()
        if s == "OK" or s.startswith("ERR"):
            return s
    return None

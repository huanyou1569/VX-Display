"""
BLE 客户端 —— 封装 bleak，通过后台 asyncio 线程与 E104-BT52 通信。
"""

import asyncio
import queue
import threading
import tkinter as tk

import protocol
from bleak import BleakScanner, BleakClient

# E104-BT52 自定义 UUID（16-bit 展开为 128-bit 蓝牙 UUID）
SERVICE_UUID = "0000fff0-0000-1000-8000-00805f9b34fb"
TX_CHAR_UUID = "0000fff1-0000-1000-8000-00805f9b34fb"  # Notify  MCU → PC
RX_CHAR_UUID = "0000fff2-0000-1000-8000-00805f9b34fb"  # Write   PC → MCU

# 传输参数 —— 匹配 UART 115200 波特率 (约 11.5 KB/s)
# 每包 120 字节 + 15ms 间隔 = ~8 KB/s，留 30% 余量
BLE_CHUNK_SIZE = 120
BLE_CHUNK_DELAY = 0.015
DATA_CMD_PRE_DELAY = 0.5           # :DATA 指令发出后等模块清空缓冲区


class BLEClient:
    """E104-BT52 BLE 通信客户端。"""

    def __init__(self, root: tk.Tk):
        self._root = root
        self._loop: asyncio.AbstractEventLoop | None = None
        self._client: BleakClient | None = None
        self._thread: threading.Thread | None = None
        self._connected = False

        self.on_notify = None   # (text: str) -> None
        self.on_status = None   # (msg: str) -> None

        # 实际使用的特征值 UUID（连接时从模块发现结果中更新）
        self._tx_uuid = TX_CHAR_UUID
        self._rx_uuid = RX_CHAR_UUID

        self._notify_queue: queue.Queue[str] = queue.Queue()
        self._status_queue: queue.Queue[str] = queue.Queue()

    # ==================================================================
    # 生命周期
    # ==================================================================

    def start(self):
        self._thread = threading.Thread(
            target=self._run_loop, daemon=True, name="ble-asyncio"
        )
        self._thread.start()

    def stop(self):
        if self._loop and self._loop.is_running():
            self._loop.call_soon_threadsafe(self._loop.stop)
        if self._thread:
            self._thread.join(timeout=2.0)

    def _run_loop(self):
        self._loop = asyncio.new_event_loop()
        asyncio.set_event_loop(self._loop)
        self._loop.run_forever()

    # ==================================================================
    # 主线程轮询
    # ==================================================================

    def poll(self):
        self._drain_queue(self._notify_queue, self.on_notify)
        self._drain_queue(self._status_queue, self.on_status)

    @staticmethod
    def _drain_queue(q: queue.Queue, callback):
        if callback is None:
            while not q.empty():
                try: q.get_nowait()
                except queue.Empty: break
            return
        while True:
            try: item = q.get_nowait()
            except queue.Empty: break
            callback(item)

    # ==================================================================
    # 内部辅助
    # ==================================================================

    def _post_status(self, msg: str):
        self._status_queue.put(msg)

    def _on_notify_raw(self, _sender, data: bytes):
        text = data.decode("ascii", errors="replace")
        self._notify_queue.put(text)

    def _post(self, coro, on_done):
        async def _wrapper():
            try: return await coro
            except Exception as exc: return exc

        future = asyncio.run_coroutine_threadsafe(_wrapper(), self._loop)

        def _callback(f):
            try: result = f.result()
            except Exception as exc: result = exc
            self._root.after(0, on_done, result)

        future.add_done_callback(_callback)

    # ==================================================================
    # 扫描 (无过滤, 按信号强度排序, 回调每发现一台就通知 UI)
    # ==================================================================

    def scan(self, on_done, timeout: float = 4.0):
        """扫描 BLE 设备，按信号强度排序。

        on_done(List[(name, address, rssi)]): 设备列表，rssi 越大越近。
        扫描期间通过 on_status 回调报告实时发现。
        """

        async def _scan():
            devices: dict[str, tuple[str, str, int]] = {}

            def _found(device, adv):
                if device.address not in devices:
                    # 过滤乱码：只保留可打印 ASCII，其余丢弃
                    raw = device.name or ""
                    name = "".join(
                        c for c in raw if 0x20 <= ord(c) <= 0x7E
                    )
                    rssi = adv.rssi if adv and adv.rssi is not None else -100
                    devices[device.address] = (name, device.address, rssi)
                    self._post_status(
                        f"发现设备: {device.address} RSSI:{rssi} {name}"
                    )

            self._post_status(f"正在扫描 ({timeout}秒)...")
            scanner = BleakScanner(_found)
            await scanner.start()
            await asyncio.sleep(timeout)
            await scanner.stop()

            # 按信号强度降序排列
            result = sorted(devices.values(), key=lambda x: x[2], reverse=True)
            self._post_status(f"扫描完成: {len(result)} 台设备")
            return result

        self._post(_scan(), on_done)

    # ==================================================================
    # 连接 / 断开
    # ==================================================================

    def connect(self, address: str, on_done):
        """连接到指定地址的 BLE 设备。"""

        async def _connect():
            self._post_status(f"正在连接 {address} ...")

            self._client = BleakClient(address, timeout=15.0)

            try:
                await self._client.connect()
            except asyncio.TimeoutError:
                raise RuntimeError("连接超时 — 设备不在范围内或已被占用")
            except Exception as e:
                raise RuntimeError(
                    f"连接失败: {e}\n"
                    f"请检查:\n"
                    f"  1. 手机是否已断开 E104-BT52\n"
                    f"  2. STM32 是否已上电\n"
                    f"  3. 地址是否正确 ({address})\n"
                    f"  4. Windows 蓝牙设置中是否已配对(先取消配对)"
                )

            # --- 打印所有服务和特征值用于调试 ---
            svc_list: list[str] = []
            tx_found = None
            rx_found = None

            for svc in self._client.services:
                svc_list.append(f"  Service: {svc.uuid}")
                for ch in svc.characteristics:
                    props = ",".join(ch.properties)
                    svc_list.append(
                        f"    Char: {ch.uuid}  [{props}]"
                    )
                    # 匹配 TX (Notify) 和 RX (Write) 特征值
                    uuid_str = str(ch.uuid).lower()
                    if "fff1" in uuid_str and "notify" in props:
                        tx_found = ch.uuid
                    if "fff2" in uuid_str and ("write" in props):
                        rx_found = ch.uuid

            svc_list.append(f"  TX候选: {tx_found}")
            svc_list.append(f"  RX候选: {rx_found}")
            self._post_status("\n".join(svc_list))

            if tx_found is None:
                await self._client.disconnect()
                raise RuntimeError(
                    "未找到 Notify 特征值 (FFF1)\n"
                    "请检查 E104-BT52 是否已配置透传模式:\n"
                    "  AT+TRANMD=1"
                )

            # 用实际发现的 UUID 替换默认值
            self._tx_uuid = tx_found
            if rx_found is not None:
                self._rx_uuid = rx_found

            # 订阅 TX 通知
            await self._client.start_notify(self._tx_uuid, self._on_notify_raw)

            self._connected = True
            self._post_status("已连接")
            return True

        self._post(_connect(), on_done)

    def disconnect(self, on_done=None):

        async def _disconnect():
            self._connected = False
            if self._client and self._client.is_connected:
                try:
                    await self._client.disconnect()
                except Exception:
                    pass
            self._post_status("已断开")
            return True

        self._post(_disconnect(), on_done or (lambda r: None))

    # ==================================================================
    # 发送指令
    # ==================================================================

    def send_command(self, cmd: bytes, on_done=None):

        async def _send():
            if not self._client or not self._client.is_connected:
                raise RuntimeError("设备未连接")
            await self._client.write_gatt_char(
                self._rx_uuid, cmd, response=False
            )
            return True

        self._post(_send(), on_done or (lambda r: None))

    # ==================================================================
    # 发送体素数据
    # ==================================================================

    def send_voxel_data(self, file_data: bytes, on_progress, on_done):

        total = len(file_data)

        async def _send():
            if not self._client or not self._client.is_connected:
                raise RuntimeError("设备未连接")

            self._post_status(f"开始体素传输: {total} 字节")
            cmd = protocol.cmd_data(total)
            await self._client.write_gatt_char(self._rx_uuid, cmd, response=False)
            await asyncio.sleep(DATA_CMD_PRE_DELAY)

            sent = 0
            while sent < total:
                chunk = file_data[sent:sent + BLE_CHUNK_SIZE]
                await self._client.write_gatt_char(
                    self._rx_uuid, chunk, response=False
                )
                sent += len(chunk)
                self._root.after(0, on_progress, sent, total)
                await asyncio.sleep(BLE_CHUNK_DELAY)

            self._post_status(f"传输完成: {total} 字节")
            return True

        self._post(_send(), on_done)

    # ==================================================================
    # 发送文件到TF卡 (:SAVE 指令)
    # ==================================================================

    def send_file_to_device(self, file_data: bytes, filename: str,
                            on_progress, on_done):
        """发送文件到设备TF卡，写入后不加载。

        filename - 目标文件名（不含路径），如 "chiken.slices"
        """

        total = len(file_data)

        async def _send():
            if not self._client or not self._client.is_connected:
                raise RuntimeError("设备未连接")

            self._post_status(
                f"开始文件传输: {filename} ({total} 字节)"
            )
            cmd = protocol.cmd_save(filename, total)
            await self._client.write_gatt_char(
                self._rx_uuid, cmd, response=False
            )
            await asyncio.sleep(DATA_CMD_PRE_DELAY)

            sent = 0
            while sent < total:
                chunk = file_data[sent:sent + BLE_CHUNK_SIZE]
                await self._client.write_gatt_char(
                    self._rx_uuid, chunk, response=False
                )
                sent += len(chunk)
                self._root.after(0, on_progress, sent, total)
                await asyncio.sleep(BLE_CHUNK_DELAY)

            self._post_status(f"文件传输完成: {total} 字节")
            return True

        self._post(_send(), on_done)

    # ==================================================================
    # 属性
    # ==================================================================

    @property
    def is_connected(self) -> bool:
        return self._connected

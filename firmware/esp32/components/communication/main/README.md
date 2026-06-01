# ESP32 Wi-Fi 管理器模块

这个模块提供简单易用的 Wi-Fi 连接、NTP 时间同步和 TCP/UDP 网络服务功能。支持两种工作模式：电脑主动请求模式和按钮触发模式。

## 功能特性

- **Wi-Fi 连接**：自动连接配置的网络，支持断线重连
- **NTP 时间同步**：从网络获取真实时间，支持荷兰时区（CET/CEST）
- **UDP 广播**：自动广播设备 IP，方便电脑端发现
- **TCP 服务器**：接收客户端连接，通过回调获取传感器数据并发送
- **按钮触发模式**：按下按钮时主动通知电脑获取数据
- **模块化设计**：简单的 API 接口，易于集成

## 工作模式

### 模式一：电脑主动请求（默认）
1. 电脑发现 ESP32 并发送 `GET_DATA` 请求
2. ESP32 读取传感器数据并返回

### 模式二：按钮触发（推荐）
1. 电脑启动后注册自己的 IP 地址
2. 按钮按下时，ESP32 通过 UDP 通知电脑
3. 电脑收到通知后发送请求获取数据

## 文件说明

| 文件 | 说明 |
|------|------|
| `wifi_manager.h` | 头文件，包含所有 API 接口 |
| `wifi_manager.c` | 实现文件，包含 Wi-Fi 和网络的完整实现 |
| `main.cpp` | 使用示例，包含按钮触发示例代码 |

## 快速开始

### 1. 配置 Wi-Fi

在 `main.cpp` 中修改 Wi-Fi 配置：

```cpp
#define WIFI_SSID     "你的WiFi名称"
#define WIFI_PASSWORD "你的WiFi密码"
```

### 2. 实现数据提供回调

其他模块（如传感器模块）需要实现此函数，提供要发送的数据：

```cpp
void sensor_data_provider(char* buffer, size_t buffer_size) {
    // 读取传感器数据
    float bodyTemp = read_body_temperature();
    float externalTemp = read_external_temperature();
    int heartRate = read_heart_rate();
    
    // 获取当前时间
    char time_str[20];
    wifi_manager_get_time_string(time_str, sizeof(time_str));
    
    // 格式: 体温,外部温度,时间,心率
    snprintf(buffer, buffer_size, "%.1f,%.1f,%s,%d",
             bodyTemp, externalTemp, time_str, heartRate);
}
```

### 3. 按钮触发（集成时使用）

当按钮被按下时，调用以下函数通知电脑：

```cpp
// 在按钮中断处理函数中调用
void button_isr_handler(void* arg) {
    // 通知电脑数据已准备好
    wifi_manager_notify_data_ready(NULL, 9998);
}
```

或者在主循环中检测按钮状态：

```cpp
void button_task(void* pvParameters) {
    while (1) {
        if (gpio_get_level(BUTTON_PIN) == 0) {  // 按钮按下
            wifi_manager_notify_data_ready(NULL, 9998);
            vTaskDelay(pdMS_TO_TICKS(500));  // 防抖
        }
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

### 4. 初始化并使用

```cpp
extern "C" void app_main() {
    // Wi-Fi 配置
    wifi_manager_config_t wifi_cfg = {
        .ssid = WIFI_SSID,
        .password = WIFI_PASSWORD
    };
    
    // 1. 初始化 Wi-Fi
    wifi_manager_init(&wifi_cfg, wifi_status_handler);
    
    // 2. 等待连接
    while (!wifi_manager_is_connected()) {
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    
    // 3. 等待时间同步
    wifi_manager_wait_for_time_sync(10000);
    
    // 4. 启动 UDP 广播（用于电脑发现）
    wifi_manager_start_discovery(9999);
    
    // 5. 启动 TCP 服务器
    wifi_manager_start_server(8888, sensor_data_provider);
    
    // 6. 按钮触发示例（实际使用时替换为真实按钮检测）
    while (1) {
        // 检测到按钮按下时调用：
        // wifi_manager_notify_data_ready(NULL, 9998);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}
```

## API 接口说明

### 初始化与配置

#### `wifi_manager_init()`
```c
bool wifi_manager_init(const wifi_manager_config_t* config, wifi_status_cb_t status_cb);
```
- **功能**：初始化 Wi-Fi 管理器，开始连接 Wi-Fi
- **参数**：
  - `config`：Wi-Fi 配置（SSID 和密码）
  - `status_cb`：连接状态回调函数（可为 NULL）
- **返回**：true 成功，false 失败

#### `wifi_manager_is_connected()`
```c
bool wifi_manager_is_connected(void);
```
- **功能**：检查 Wi-Fi 是否已连接
- **返回**：true 已连接，false 未连接

#### `wifi_manager_get_ip()`
```c
void wifi_manager_get_ip(char* buffer, size_t buffer_size);
```
- **功能**：获取当前 IP 地址
- **参数**：
  - `buffer`：存储 IP 的缓冲区
  - `buffer_size`：缓冲区大小

### 时间同步

#### `wifi_manager_wait_for_time_sync()`
```c
bool wifi_manager_wait_for_time_sync(uint32_t timeout_ms);
```
- **功能**：等待 NTP 时间同步完成
- **参数**：`timeout_ms`：超时时间（毫秒）
- **返回**：true 同步成功，false 超时或失败

#### `wifi_manager_get_time_string()`
```c
void wifi_manager_get_time_string(char* buffer, size_t buffer_size);
```
- **功能**：获取当前时间字符串
- **格式**：`DD-MM-YYYY HH:MM:SS`（如 `01-06-2026 14:30:25`）
- **时区**：荷兰时区（CET/CEST，自动处理夏令时）

### 网络服务

#### `wifi_manager_start_discovery()`
```c
bool wifi_manager_start_discovery(uint16_t port);
```
- **功能**：启动 UDP 广播服务，用于电脑端自动发现 ESP32
- **参数**：`port`：UDP 广播端口号
- **广播格式**：`ESP32_HEALTH:<IP>:<TCP_PORT>`
- **返回**：true 成功，false 失败

#### `wifi_manager_start_server()`
```c
bool wifi_manager_start_server(uint16_t port, data_provider_cb_t provider_cb);
```
- **功能**：启动 TCP 服务器，等待客户端连接并发送数据
- **参数**：
  - `port`：TCP 端口号
  - `provider_cb`：数据提供回调函数（传感器模块实现）
- **返回**：true 成功，false 失败

### 按钮触发

#### `wifi_manager_notify_data_ready()`
```c
bool wifi_manager_notify_data_ready(const char* pc_ip, uint16_t pc_port);
```
- **功能**：通知电脑数据已准备好（在按钮按下时调用）
- **参数**：
  - `pc_ip`：电脑 IP 地址（如果为 NULL，使用已保存的 IP）
  - `pc_port`：电脑接收通知的 UDP 端口
- **返回**：true 成功，false 失败
- **说明**：电脑需要先连接 ESP32 并发送 `PC_IP:<ip>:<port>` 注册自己的 IP

#### `wifi_manager_set_pc_ip()`
```c
void wifi_manager_set_pc_ip(const char* pc_ip);
```
- **功能**：设置电脑 IP 地址（通常由系统自动调用）
- **参数**：`pc_ip`：电脑 IP 地址字符串

## 回调函数类型

### 数据提供回调
```c
typedef void (*data_provider_cb_t)(char* buffer, size_t buffer_size);
```
- **说明**：传感器模块实现此函数，将数据写入 buffer
- **数据格式**：建议使用 CSV 格式，如 `36.5,24.3,01-06-2026 14:30:25,72`

### 状态回调
```c
typedef void (*wifi_status_cb_t)(bool connected, const char* ip_address);
```
- **说明**：Wi-Fi 连接状态变化时调用
- **参数**：
  - `connected`：true 已连接，false 已断开
  - `ip_address`：IP 地址（断开时为 NULL）

## 数据格式

传感器数据建议使用以下 CSV 格式：

```
体温,外部温度,时间,心率
36.5,24.3,01-06-2026 14:30:25,72
```

## 电脑端使用

### 按钮触发模式

电脑端程序 `pc_client.py` 会自动：
1. 监听 UDP 广播发现 ESP32
2. 连接 ESP32 并注册自己的 IP 地址
3. 等待按钮按下通知
4. 收到通知后发送请求获取数据
5. 保存到 `sensor_data.txt`

运行：
```bash
python pc_client.py
```

然后按下 ESP32 上的按钮，电脑会自动接收数据。

### 传统请求模式

如果需要电脑主动请求数据，修改 `pc_client.py` 中的代码，使用手动发送请求的方式。

## 编译烧录

使用 ESP-IDF：

```powershell
idf.py fullclean
idf.py build
idf.py -p COM5 flash
idf.py -p COM5 monitor
```

## 注意事项

1. **Wi-Fi 配置**：使用前务必修改 `WIFI_SSID` 和 `WIFI_PASSWORD`
2. **时区设置**：默认使用荷兰时区，如需修改请编辑 `wifi_manager.c` 中的 `setenv("TZ", ...)`
3. **时间格式**：默认格式为 `DD-MM-YYYY HH:MM:SS`，如需修改请编辑 `wifi_manager_get_time_string()`
4. **按钮触发**：`wifi_manager_notify_data_ready()` 需要在按钮按下时调用，参考 `main.cpp` 中的示例
5. **电脑 IP**：按钮触发模式下，电脑需要先连接 ESP32 一次以注册 IP 地址

## 示例输出

ESP32 串口输出（按钮触发模式）：
```
Wi-Fi 已连接，IP: 192.168.1.100
NTP 时间同步完成
TCP 服务器启动: 192.168.1.100:8888
UDP 广播: ESP32_HEALTH:192.168.1.100:8888
电脑客户端已连接
PC registered: 192.168.1.50:9998
[Simulated Button Press #1] Notifying PC...
Data ready notification sent to 192.168.1.50:9998
收到请求: GET_DATA
发送响应: 36.5,24.3,01-06-2026 14:30:25,72
```

## 集成指南

### 对于传感器模块开发者

1. 实现 `sensor_data_provider()` 函数，读取传感器并格式化数据
2. 在按钮中断或检测逻辑中调用 `wifi_manager_notify_data_ready()`

### 对于主程序开发者

1. 调用 `wifi_manager_init()` 初始化 Wi-Fi
2. 调用 `wifi_manager_start_server()` 启动 TCP 服务器
3. 调用 `wifi_manager_start_discovery()` 启动 UDP 广播
4. 创建按钮检测任务或中断，在按钮按下时调用 `wifi_manager_notify_data_ready()`

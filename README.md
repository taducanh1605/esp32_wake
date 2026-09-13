# ESP32 Wake

ESP32 Wake là firmware cho board ESP32-C3 để:
- kết nối vào WiFi của mạng LAN,
- kiểm tra trạng thái máy mục tiêu bằng ping,
- gửi lệnh wake bằng relay,
- bật/tắt máy mục tiêu qua thao tác HTTP,
- phục vụ giao diện cấu hình WiFi qua Access Point nếu chưa có cấu hình.

## Tính năng chính

- Kết nối WiFi STA với SSID/password đã lưu.
- Nếu chưa có cấu hình, board tự mở Access Point để dễ cấu hình.
- Scan WiFi có thể thử, nhưng không phải lúc nào cũng trả về danh sách. Nếu không thấy, vẫn có thể nhập SSID thủ công.
- Kiểm tra máy cần wake bằng ICMP ping tới IP mục tiêu.
- Relay kích hoạt ngắn để wake máy.
- Tắt máy bằng relay kéo dài 7 giây.
- Reset toàn bộ cài đặt khi giữ nút BOOT/RESET khoảng 15 giây.

## Mạch / chân GPIO

Trong code hiện tại:

- Relay: GPIO 5
- Power Sense: GPIO 12
- Reset hold detect: GPIO 9 (BOOT/RESET logic)

## Cấu trúc project

```text
esp32_wake/
├─ src/
│  └─ main.cpp
├─ platformio.ini
├─ README.md
├─ flash.ps1
├─ flash.bat
└─ .pio/            (tạo sau khi build)
```

## Yêu cầu môi trường

- Windows 10/11
- Visual Studio Code
- PlatformIO Core hoặc PlatformIO IDE extension
- Cổng USB dành cho ESP32-C3

## Cài đặt PlatformIO

Nếu chưa có PlatformIO, cài đặt theo một trong các cách sau:

### Cách 1: PlatformIO IDE trong VS Code
- Mở VS Code
- Cài extension: PlatformIO IDE
- Mở folder project
- Build/upload sẽ tự nhận môi trường

### Cách 2: PlatformIO Core
```powershell
pip install platformio
```

Kiểm tra:
```powershell
pio --version
```

## Build và flash board

### Cách nhanh nhất
Từ thư mục project, chạy script:

```powershell
.lash.ps1
```

Nếu COM port cần chỉ định rõ:

```powershell
.lash.ps1 COM4
```

Script cũng hỗ trợ `.bat`:

```bat
flash.bat
```

### Cách thủ công
```powershell
pio run -e esp32-c3-devkitm-1
pio run -e esp32-c3-devkitm-1 --target upload --upload-port COM4
```

Nếu board đang ở cổng khác, đổi `COM4` thành cổng thực tế của bạn.

Để xem port đang có:

```powershell
pio device list
```

## Kết nối với board

### Chế độ Access Point (AP)
Khi chưa lưu WiFi hoặc không kết nối được WiFi:

- SSID: `ESP32-Wake-Setup`
- Password: `12345678`

Mở trình duyệt và vào giao diện cấu hình.

### Chế độ Station (STA)
Khi đã lưu WiFi thành công, board tự kết nối vào mạng WiFi đó và mở máy chủ web ở IP local của ESP32.

Ví dụ nếu board nhận IP là `192.168.1.28`, lúc đó bạn có thể truy cập:

```text
http://192.168.1.28/
http://192.168.1.28/status
http://192.168.1.28/wake
http://192.168.1.28/shutdown
```

Trang cấu hình (`GET /`) luôn có thể truy cập được qua port này, kể cả sau khi đã cấu hình xong. Nếu đã đặt "Mật khẩu điều khiển" lúc setup, trang này sẽ yêu cầu đăng nhập trước khi cho xem/sửa cấu hình (đăng nhập một lần, phiên làm việc lưu bằng cookie cho tới khi board khởi động lại).

### IPv6

Ở chế độ STA, firmware tự bật IPv6 và HTTP/HTTPS server ưu tiên listener dual-stack trên cùng các port đã cấu hình. Nếu lwIP không tạo được socket dual-stack, server tự quay về IPv4 để không làm mất đường cấu hình.

Serial log sẽ in từng địa chỉ IPv6 được cấp. Địa chỉ `fe80::/10` chỉ dùng trong cùng link; muốn truy cập từ Internet, router/ISP phải quảng bá một địa chỉ IPv6 global và firewall của router phải cho phép port tương ứng.

Khi truy cập trực tiếp bằng địa chỉ IPv6, đặt địa chỉ trong dấu ngoặc vuông:

```text
http://[2001:db8:1234::abcd]:2443/
https://[2001:db8:1234::abcd]:2444/
```

Schema API hiện vẫn giữ trường `device_ip` là IPv4 để tương thích với các dashboard hiện có.

### Tự động kết nối lại WiFi
Khi mất kết nối, board sẽ tự thử kết nối lại vào WiFi đã lưu; nếu vẫn thất bại, board chuyển sang chế độ AP để cấu hình lại. Nếu tùy chọn "Tự động kết nối lại WiFi" (mặc định bật) đang bật, board còn tự thử kết nối lại mỗi 1 phút trong lúc ở chế độ AP, nhưng chỉ khi chưa có ai đang kết nối vào AP đó — nhờ vậy board có thể tự phục hồi sau khi mất mạng/router khởi động lại mà không cần đến tận nơi cấu hình.

## Cấu hình WiFi và target

Trang cấu hình cho phép nhập:
- SSID WiFi
- Mật khẩu WiFi
- IP máy cần wake
- Port web server

Nếu không scan ra mạng WiFi nào, hãy nhập SSID thủ công ở ô tùy chỉnh.

## API / Endpoint

Xem hợp đồng API đầy đủ, ma trận xác thực, aliases, CORS và ví dụ tích hợp tại [ESP32_API.md](ESP32_API.md).

### 1. GET /
Trả về:
- HTML cấu hình (kể cả trong chế độ STA, không chỉ AP), hoặc
- Trang đăng nhập nếu đã đặt mật khẩu điều khiển và chưa đăng nhập.

### 1b. POST /login
Gửi `password` để đăng nhập vào trang cấu hình khi đã đặt mật khẩu điều khiển. Thành công sẽ chuyển hướng về `/` kèm cookie phiên làm việc.

### 2. GET or POST /status
Khi chưa cấu hình mật khẩu điều khiển, dùng `GET`. Khi đã cấu hình mật khẩu, dùng `POST` với trường form `password`. Request sai phương thức hoặc sai mật khẩu sẽ bị từ chối.

```json
{
  "wifi_connected": true,
  "ssid": "Pika-box_2G",
  "target_ip": "192.168.1.135",
  "port": 2443,
  "device_ip": "192.168.1.28",
  "target_online": true
}
```

Alias:
- `/stt`

### 3. GET /wake
Gửi lệnh wake máy bằng relay ngắn.

Alias:
- `/pw`

Ví dụ:
```text
http://192.168.1.28/wake
```

Trả về:
```json
{"status":"wake-triggered"}
```

### 4. GET /shutdown
Gửi lệnh tắt máy bằng relay kéo dài 7 giây.

Alias:
- `/sd`
- `/fsd`

Ví dụ:
```text
http://192.168.1.28/shutdown
```

Trả về:
```json
{"status":"shutdown-triggered"}
```

### 5. GET /api/scan
Quét mạng WiFi xung quanh.

Trả về JSON dạng:
```json
{"networks":[{"ssid":"Pika-box_2G","rssi":-52}]}
```

Lưu ý: trên ESP32-C3, scan không phải lúc nào cũng trả dữ liệu đầy đủ tùy điều kiện mạng; nếu không có kết quả, nên nhập SSID thủ công.

### 6. POST /api/config
Lưu cấu hình WiFi và target.

Các tham số:
- `ssid_custom` hoặc `ssid`
- `password`
- `port`
- `target_ip`

Ví dụ gửi bằng browser form hoặc curl:

```powershell
curl.exe -X POST "http://192.168.1.28/api/config" -d "ssid_custom=Pika-box_2G&password=yourpass&port=2443&target_ip=192.168.1.135"
```

## Reset cài đặt

Để xóa toàn bộ setting đã lưu:

1. Giữ nút BOOT/RESET trên board.
2. Giữ khoảng 15 giây.
3. Board sẽ xóa dữ liệu WiFi và target.
4. Board sẽ vào chế độ cấu hình AP lại.

## Ghi chú

- `target_online` được xác định bằng ping tới IP mục tiêu, không phải bằng thử kết nối TCP đến port.
- Ping dùng 3 lần thử, mỗi lần timeout ngắn (~250ms) thay vì 1 lần chờ lâu, để tránh báo sai "offline" chỉ vì rớt một gói tin, đồng thời vẫn phản hồi nhanh.
- Nếu bộ định tuyến hoặc mạng có chặn ICMP, trạng thái online có thể không đúng. Trong môi trường LAN thông thường, ping là cách kiểm tra đáng tin cậy nhất.
- Board này đang dùng PlatformIO với board `esp32-c3-devkitm-1`.

## Troubleshooting

### Không thấy cổng COM
- Kiểm tra driver USB-serial cho ESP32-C3
- Thử ngắt và cắm lại board
- Chạy:

```powershell
pio device list
```

### Không kết nối WiFi
- Kiểm tra SSID/password
- Kiểm tra mật khẩu đúng và không có ký tự đặc biệt gây lỗi
- Nếu cần, vào AP `ESP32-Wake-Setup` và nhập lại

### Không thấy mạng WiFi khi quét
- Đây là hiện tượng thường gặp trên ESP32-C3, tùy nền tảng và điều kiện mạng
- Hãy sử dụng nhập SSID thủ công

## License

Project này dùng theo giấy phép tùy chọn của bạn (nếu chưa có, bạn có thể thêm MIT hoặc Apache-2.0 tùy mục đích phát triển).

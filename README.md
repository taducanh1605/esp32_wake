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

Firmware dùng partition `min_spiffs.csv`: mỗi vùng app 1.875 MiB trên flash 4 MiB, giữ nguyên vùng NVS chứa cấu hình. Image nhị phân có thể lớn hơn thống kê code của PlatformIO; `build_release.ps1` kiểm tra kích thước file thực tế trước khi đóng gói để tránh boot loop do image vượt partition.

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

### HTTPS certificate confirmation

The dashboard's certificate confirmation button opens `GET /certificate-check` using the saved HTTPS address and port. After you accept the browser warning for your own ESP32, this page attempts to close its tab automatically. No password or configuration session is required or changed. If the browser or mobile PWA handoff prevents closing, it displays "Connection confirmed" and you can close the tab manually. This does not install a trusted certificate or bypass TLS verification; an exception in another browser context may not apply to the PWA.

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

## Tự nhận IP máy tính và hostname DuckDNS

Hai tính năng này mặc định **tắt** để giữ nguyên cấu hình cũ. Sau khi flash firmware mới:

1. Mở trang config của ESP32. Khi dùng WiFi STA, ưu tiên HTTPS, ví dụ `https://192.168.1.94:2444/`.
2. Tích **Automatically detect the computer connected over USB** nếu dùng helper Windows. Khi bỏ tích, nhập IPv4 LAN và/hoặc IPv6 global của PC vào hai ô thủ công.
3. Giữ **Use IPv6** bật nếu muốn dùng DDNS IPv6.
4. Đăng nhập [DuckDNS](https://www.duckdns.org/), đăng ký hai subdomain khác nhau, ví dụ `mypc-wake` và `mypc-stream`. Đăng ký hostname là bước thực hiện trên DuckDNS; firmware chỉ cập nhật IP, không tạo tài khoản/hostname qua API.
5. Tích **Automatically update IPv6 hostnames (DuckDNS)**, nhập account token và hai hostname đã đăng ký. Có thể nhập subdomain hoặc tên đầy đủ có đuôi `.duckdns.org`.
6. Save configuration. Ô token luôn để trống sau khi tải trang; để trống khi sửa cấu hình sẽ giữ token đã lưu.

Khi bật cập nhật DuckDNS, phần đầu thông tin mạng ưu tiên hostname và trạng thái DNS:

- ESP32: URL HTTPS, ví dụ `https://mypc-wake.duckdns.org:2444/`. Cổng luôn lấy từ cổng HTTPS thực tế; nếu HTTP là `65535`, HTTPS là `65534`.
- PC: `mypc-stream.duckdns.org`, dùng làm hostname khi thêm PC trong Moonlight/Artemis.
- Trạng thái cập nhật của từng hostname và trạng thái helper được hiển thị riêng.

IPv4/IPv6 và các URL kết nối nằm trong mục **IP addresses and connection URLs**, tự thu gọn khi bật DuckDNS nhưng vẫn có thể mở để kiểm tra. Khi tắt DuckDNS, hostname/trạng thái DNS và các ô cấu hình DuckDNS được ẩn, mục địa chỉ IP tự mở lại. Bật/tắt checkbox cập nhật phần hiển thị ngay; thay đổi cấu hình trên mạch vẫn cần Save configuration. Refresh không đóng lại mục IP nếu bạn đã mở thủ công.

ESP32 ưu tiên IPv6 global cho ping PC, có fallback IPv4 LAN. Helper gửi cả hai IP; không chọn VPN/adapter ảo mặc định, loại link-local/ULA và ưu tiên IPv6 ổn định hơn temporary address trên adapter phù hợp. Nếu chỉ có temporary global IPv6, helper vẫn dùng nó và theo dõi thay đổi.

### Giảm cập nhật DuckDNS và đổi prefix IPv6

ESP32 quản lý DDNS cho cả hai hostname; helper Windows chỉ gửi IP PC qua USB. Firmware truy vấn bản ghi AAAA bằng DNS-over-HTTPS (`dns.google`) trước khi ghi DuckDNS. Nếu IPv6 đã trùng thì không gọi API update, kể cả sau reboot. Khi IP không đổi, mỗi hostname được kiểm tra DNS khoảng 5 phút một lần; khi IP đổi sẽ kiểm tra sớm hơn. So sánh theo địa chỉ nhị phân nên IPv6 rút gọn và viết đầy đủ được coi là giống nhau. Nếu DNS/TLS lỗi, firmware không tự gửi update mà thử lại với backoff.

Firmware không còn gọi `clear=true` trước khi update, tránh xóa bản ghi rồi tạo lại gây gián đoạn. Bản ghi A IPv4 có sẵn không được chủ động xóa; nếu chỉ muốn dùng IPv6, kiểm tra/xóa IPv4 không phù hợp trên trang DuckDNS. DNS cache vẫn có thể giữ IPv6 cũ đến hết TTL sau khi IP thực sự thay đổi.

Khi prefix `/64` của ESP32 thay đổi, firmware thay 64 bit đầu của IPv6 PC và giữ nguyên 64 bit cuối, nhưng **chỉ khi IPv6 PC khớp prefix ESP32 đã biết trước đó**. Prefix cũ được lưu để xử lý được cả sau reboot; nếu chưa có prefix cũ hoặc PC thuộc subnet khác, firmware không đoán. ESP32 ưu tiên IPv6 global vừa nhận khi router còn giữ cả địa chỉ cũ và mới. Địa chỉ PC suy ra được dùng cho ping/DDNS ngay cả khi PC đang ngủ và helper không gửi heartbeat.

Đây là địa chỉ **suy ra**, không phải firmware thay cấu hình mạng trên Windows. Cách này giả định PC giữ nguyên phần định danh 64 bit cuối. Windows có thể đổi phần này khi prefix đổi hoặc khi dùng temporary/privacy address; khi PC hoạt động, IP thực tế helper báo về sẽ thay thế địa chỉ suy ra. Mất heartbeat vẫn chặn DDNS PC trong chế độ tự detect nếu chưa có địa chỉ suy ra từ một lần đổi prefix đã xác nhận.

### Cài helper Windows 10/11

Hướng dẫn cài nhanh và xử lý cảnh báo Windows: [windows/INSTALL.md](windows/INSTALL.md).

Không cần cài Python, PlatformIO hay module PowerShell cho helper. Dùng cả thư mục `windows` trong project hoặc release.

**Khuyến nghị cho wake từ xa, hoạt động trước khi đăng nhập Windows:**

1. Cắm ESP32 bằng cáp USB có truyền dữ liệu. Đóng Serial Monitor và phần mềm đang mở COM.
2. Nhấp phải `windows/install-helper.bat`, chọn **Run as administrator**. Tự bạn xác nhận UAC trên Windows.
3. Bộ cài tạo task `ESP32WakeHelper`, chạy khi Windows khởi động bằng SYSTEM, kể cả khi chưa đăng nhập. Script nằm tại `%ProgramFiles%\ESP32Wake`, với quyền ghi chỉ cho Administrators/SYSTEM. Helper khởi chạy ngay sau cài đặt.
4. Bật tự detect trên config ESP32 và kiểm tra trạng thái **Helper connected**. Nếu ESP32 khởi động chậm hơn PC, helper tự thử lại.

Tương đương, trong PowerShell **đã được bạn mở bằng quyền admin**:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\windows\install-helper.ps1 -AtBoot
```

**Cách không cần admin:** nhấp đúp `windows/install-helper.bat`. Helper khởi chạy ngay, rồi tự chạy khi tài khoản Windows hiện tại đăng nhập thông qua Startup shortcut. Cách này **không** gửi IP sau cold boot cho đến khi đăng nhập, nên không phù hợp nếu cần streaming từ màn hình đăng nhập sau khi IPv6 đổi.

Bộ cài chỉ dùng `ExecutionPolicy Bypass` cho tiến trình helper; không đổi execution policy toàn hệ thống. Không gửi mật khẩu hay token DuckDNS cho helper, không tải phần mềm phụ và không mở firewall.

Helper tự tìm USB COM của Espressif, CP210x, CH340 hoặc FTDI, sau đó xác nhận bằng handshake với firmware. Nó không mặc định dùng `COM8`. ESP32 phải đang chạy firmware mới, không ở bootloader. Chỉ một ESP32 Wake trên PC được hỗ trợ tự chọn; nếu có nhiều board hoặc USB bridge khác, chỉ định COM khi chạy thủ công.

Chạy foreground để chẩn đoán (dừng helper đang cài trước):

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\windows\esp32-wake-helper.ps1
# Chỉ khi cần chọn rõ cổng hoặc adapter:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\windows\esp32-wake-helper.ps1 -PortName COM8 -InterfaceAlias "Ethernet"
```

Helper kiểm tra mỗi 30 giây, gửi ngay khi phát hiện IP đổi và heartbeat mỗi 60 giây. Mỗi lần gửi mở COM, handshake, gửi IP, nhận ACK rồi đóng cổng ngay; không giữ COM trong lúc chờ. USB bị rút hoặc COM bận do flash/Serial Monitor sẽ được thử lại. Sau resume/boot, chờ Windows có mạng và helper gửi địa chỉ mới. Trên PC dùng helper này, bật tự detect mới cho phép bản tin COM thay đổi IP đã lưu; chế độ thủ công không bị ghi đè.

### Log, dừng và gỡ helper

- Boot mode: log `%ProgramData%\ESP32Wake\helper.log`. Khi cần tạm dừng hoàn toàn, trong Task Scheduler chọn task `ESP32WakeHelper`, **End** rồi **Disable**; bật lại và **Run** sau khi xong.
- Sign-in mode: log `%LOCALAPPDATA%\ESP32Wake\helper.log`. Bình thường không cần dừng/gỡ helper để flash vì cổng được đóng giữa các lần gửi. Nếu flash trùng đúng lúc gửi và báo COM bận, thử lại sau vài giây; chỉ gỡ/cài lại khi cần tạm dừng hoàn toàn.
- Log được xoay khi vượt 1 MB; không chứa token DuckDNS.

Gỡ boot mode trong PowerShell admin:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\windows\install-helper.ps1 -AtBoot -Uninstall
```

Gỡ sign-in mode trong PowerShell thường:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\windows\install-helper.ps1 -Uninstall
```

Cài lại bằng đúng mode để nâng cấp helper. Gỡ cài đặt giữ log để chẩn đoán.

### Điều kiện DDNS và bảo mật

- DDNS dùng AAAA cho IPv6 global, không công bố IPv4 LAN thành đường truy cập Internet. Lần cập nhật đầu của mỗi hostname sau boot/thay cấu hình sẽ xóa A/AAAA cũ rồi ghi AAAA, tránh A cũ trỏ sai; chỉ dùng hostname dành riêng cho ESP32 Wake.
- Cập nhật khi IP thay đổi và kiểm tra lại mỗi 5 phút. Lỗi mạng/TLS/API được retry có backoff; request chạy trong worker riêng, không chặn vòng lặp web. Phải đồng bộ giờ mạng bằng NTP trước khi xác minh TLS.
- Khi PC tắt hoặc helper mất kết nối, giữ DNS cuối cùng; không tự đoán IPv6 mới của PC. Sau wake và khi helper báo lại, ESP32 cập nhật hostname PC. Nếu PC mất global IPv6, hostname không tự fallback sang IPv4 private.
- Client cần IPv6; router/Windows firewall phải cho phép cổng HTTPS của ESP32 và các cổng streaming của Sunshine/Apollo. DDNS không tự mở cổng hay vượt CGNAT. Không mở giao diện quản trị Sunshine ra Internet chỉ vì cần streaming.
- Chứng chỉ HTTPS trên ESP32 vẫn là self-signed, nên đổi sang hostname không loại cảnh báo trình duyệt. Request tới DuckDNS dùng HTTPS có xác minh Amazon Root CA 1; nếu nhà cung cấp đổi CA, cần nâng cấp firmware.
- Token có thể cập nhật mọi hostname của tài khoản DuckDNS. Firmware lưu token trong Preferences/NVS, **không mã hóa flash trong bản build này**; người có quyền truy cập phần cứng có thể lấy token. Trang config không điền lại token và `/api/network` không trả token. Dùng mật khẩu quản trị mạnh, HTTPS trong STA và tài khoản DuckDNS riêng nếu cần giảm phạm vi ảnh hưởng. Reset toàn bộ cấu hình sẽ xóa token cục bộ, không xóa hostname trên DuckDNS.

### Kiểm thử không cần thiết bị

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tests\windows_helper_test.ps1
node .\tests\config_ui_test.cjs
pio run -e policy-check
```

Kiểm thử này không thay thế kiểm tra COM thật, NTP/TLS trên ESP32, firewall và cập nhật AAAA bằng tài khoản DuckDNS của bạn.

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

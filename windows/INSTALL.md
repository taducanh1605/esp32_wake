# Cài helper ESP32 Wake trên Windows

**Chạy BAT một lần để cài. Sau đó helper tự khởi động, không cần mở BAT mỗi lần.** Không cần Python hay PlatformIO; dùng Windows 10/11 có Windows PowerShell 5.1.

## Cách cài khuyến nghị

1. Tải gói release từ nguồn bạn tin cậy. Nếu dùng ZIP, nhấp phải ZIP → **Properties** → tích **Unblock** nếu có → **Apply**, rồi **Extract All**. Giữ ba file `.bat`/`.ps1` trong cùng thư mục `windows`.
2. Cắm ESP32 bằng cáp USB có truyền dữ liệu. Đóng Serial Monitor và chương trình đang mở cổng COM.
3. Nhấp phải [install-helper.bat](install-helper.bat) → **Run as administrator** → tự xác nhận **Yes** tại UAC. Chờ dòng **Installed for Windows startup, before sign-in**.
4. Trên trang config ESP32, bật **Automatically detect the computer connected over USB**, lưu cấu hình và chờ **Helper connected** cùng IPv4/IPv6 của PC.

Helper chạy ngay sau khi cài, tự tìm COM và tự thử lại nếu ESP32 chưa cắm hoặc mạng chưa sẵn sàng. Nó cũng tự chạy khi bật lại Windows, **không cần đăng nhập**. Không cần nhập mật khẩu Windows vào bộ cài.

**Không có quyền admin?** Nhấp đúp BAT để cài cho tài khoản hiện tại. Dòng thành công là **Installed for this user's sign-in**. Helper chạy ngay và tự chạy **sau khi bạn đăng nhập**, không chạy ở màn hình đăng nhập sau cold boot.

| Cách chạy BAT | Tự chạy khi nào? |
|---|---|
| Run as administrator | Khi Windows khởi động, trước đăng nhập; khuyến nghị cho wake từ xa. |
| Nhấp đúp thông thường | Khi tài khoản đã cài helper đăng nhập. |

Chỉ cần cài một chế độ. Chạy lại bộ cài để cập nhật; khi nâng từ chế độ đăng nhập lên chế độ khởi động, bộ cài tự thay thế chế độ cũ. Sau khi cài có thể xóa thư mục tải về vì helper đã được sao chép vào máy.

## Tạo hostname DuckDNS trước khi bật DDNS

**Điền hostname vào config không tự tạo tên miền.** API công khai của DuckDNS chỉ cập nhật IP/TXT; không có API đăng ký hostname mới. Helper cũng không tạo tên miền.

1. Đăng nhập [DuckDNS Domains](https://www.duckdns.org/domains).
2. Đăng ký hai tên khác nhau trong **cùng tài khoản**, ví dụ `pikapc-wake` cho ESP32 và `pikapc-stream` cho PC. Nếu tên đã có người dùng, chọn tên khác; nếu đã thuộc tài khoản của bạn, dùng lại.
3. Lấy token của tài khoản đó. Không nhập mật khẩu Google/GitHub vào ESP32 hoặc helper.
4. Trên config ESP32, bật **Automatically update IPv6 hostnames (DuckDNS)**, nhập token và hai tên đã đăng ký, rồi lưu. Có thể nhập subdomain hoặc tên đầy đủ, ví dụ `pikapc-wake.duckdns.org`.

Chỉ cần đăng ký một lần. Sau đó ESP32 tự cập nhật IPv6; helper cung cấp địa chỉ PC. Kiểm tra **ESP32 DDNS / PC DDNS: Updated** để biết request đã thành công; DNS có thể cần thêm thời gian hết cache. Nếu bị từ chối, kiểm tra tên đã được tạo trong đúng tài khoản của token.

## Nếu Windows chặn file

- **UAC:** bạn phải tự bấm **Yes** hoặc cung cấp tài khoản admin trực tiếp trong hộp thoại Windows. Bộ cài không thể tự chấp nhận hoặc bỏ qua UAC.
- **SmartScreen / Windows protected your PC:** chỉ với gói đã kiểm tra nguồn, chọn **More info → Run anyway** nếu Windows cho phép. Đây là cho phép file cụ thể, không tắt SmartScreen toàn máy. Không có nút này hoặc máy thuộc công ty: hỏi quản trị viên.
- **Open File - Security Warning:** kiểm tra đúng file/nguồn rồi chọn **Run**.
- **PowerShell bị chặn:** BAT đã dùng `-ExecutionPolicy Bypass` riêng cho tiến trình installer và helper. Không cần chạy `Set-ExecutionPolicy`, tắt antivirus, firewall hoặc sửa registry.

Nếu vẫn bị chặn do file tải từ Internet, mở PowerShell trong thư mục `windows` chứa bộ cài đã kiểm tra nguồn và chạy:

```powershell
Get-Item -LiteralPath '.\install-helper.bat', '.\install-helper.ps1', '.\esp32-wake-helper.ps1' | Unblock-File
```

Sau đó chạy lại BAT. Lệnh này chỉ bỏ dấu nguồn Internet trên ba file, không tắt bảo vệ hệ thống.

Nếu không chạy được BAT, mở **Windows PowerShell bằng Run as administrator**, chuyển tới thư mục `windows` bằng `cd`, rồi chạy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install-helper.ps1 -AtBoot
```

Không có quyền admin thì mở PowerShell thường và bỏ `-AtBoot` để cài chế độ đăng nhập:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install-helper.ps1
```

**Máy công ty, AppLocker/WDAC hoặc Group Policy có thể vẫn chặn dù dùng Bypass.** Xem `Get-ExecutionPolicy -List` và gửi lỗi cho quản trị viên để được cho phép. Không dùng các bước này để vượt chính sách tổ chức. Nếu Defender cảnh báo malware, dừng cài và kiểm tra gói; không tắt Defender hay thêm ngoại lệ để ép chạy.

## Kiểm tra và gỡ cài đặt

- Chế độ khởi động: mở Task Scheduler, tìm `ESP32WakeHelper`. Log: `%ProgramData%\ESP32Wake\helper.log`.
- Chế độ đăng nhập: nhấn **Win+R**, nhập `shell:startup`, kiểm tra shortcut `ESP32 Wake Helper`. Log: `%LOCALAPPDATA%\ESP32Wake\helper.log`.
- Log có **Connected to ESP32** và **Network IPv4=... IPv6=...; WAKE_ACK ... AUTO** là đã gửi IP thành công. Để có IPv6 global, PC và router/ISP phải có IPv6.
- Helper **không giữ COM liên tục**: chỉ mở khi gửi IP/heartbeat, rồi đóng ngay; nếu COM bận do flash hoặc Serial Monitor, helper tự thử lại. Bình thường không cần gỡ hay dừng helper trước khi flash.
- Nếu flash trùng đúng lần gửi và báo COM bận, thử lại sau vài giây. Khi cần giữ COM riêng hoàn toàn, boot mode có thể dùng **End / Disable**, rồi **Enable / Run** trong Task Scheduler; sign-in mode có thể gỡ rồi cài lại theo lệnh dưới đây.

Gỡ boot mode: mở PowerShell **admin** trong thư mục bộ cài và chạy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install-helper.ps1 -AtBoot -Uninstall
```

Gỡ sign-in mode: mở PowerShell **thường** trong thư mục bộ cài và chạy:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\install-helper.ps1 -Uninstall
```

Gỡ cài đặt giữ log. Chi tiết cấu hình ESP32 và DuckDNS: [README](../README.md).
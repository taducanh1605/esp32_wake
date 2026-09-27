# 0. Setup phần cứng:

+ bước 0: bọc băng keo giấy (hoặc bất kỳ loại nào dùng để cách điện) quanh mạch để tránh chạm chập nếu mạch cho có hộp. Không cần bọc quá kỹ vì còn cần theo dõi tìn hiệu LED khi setup
+ bước 1: mạch sẽ có 2 chân pin dùng để kích nguồn, cắm vào PWR trên mainboard (chú ý chiều +/-). Sẽ có 2 chân female mở rộng để cắm 2 pin của nút nguồn vật lý.
+ bước 2: cắm dây usb type C vào mạch, cắm đầu còn lại vào cụm pin cho usb trên mainboard

/!\ Chú ý: khi tắt máy, nếu mạch không có điện thì cần vào BIOS của máy tính, chọn chế độ giữ nguồn dành cho các cổng USB khi tắt máy.

# 1. Phát hiện Port COM của esp32 đang kết nối

+ cách 1: mở powerShell, chạy lệnh:
Get-CimInstance Win32_SerialPort | Select-Object DeviceID, Name, Status

+ cách 2: mở Run, chạy compmgmt.msc, tìm đến Device Mananger -> Port (COM & LPT)

# 2. Flash ROM

+ bước 1: mở powerShell tại folder release
+ bước 2: chạy lệnh với COMx là cổng COM tìm được từ phần 1 (vd: COM8)
./flash_merged.ps1 COMx

# 3. Config ESP32

+ bước 1: dùng 1 thiết bị kết nối với wifi ESP32-Wake-Setup, pass: 12345678
+ bước 2: truy cập trang http://192.168.4.1/, pass: 12345678
+ bước 3: Ấn nút Scan Wifi networks ở phía dưới cùng để lấy danh sách wifi, nếu không tìm thấy wifi nào thì ấn lại, sau đó chọn 1 wifi trong kết quả
+ bước 4: điền thông tin các config (các thông tin không nêu dưới đây có thể để mặc định):
	* WiFi password
	* Target computer IP (là IP của máy muốn điều khiển)
	* Use a Power LED pin to detect whether the computer is powered (chỉ tích nếu mạch có hỗ trợ đọc trạng thái LED trên máy tính, dùng cho version mới)
	* Control password (mật khẩu để tránh bất kỳ ai củng có thể điều khiển được)
+ bước 5: ấn nút Save configuration và chờ cho đến khi setup hoàn thành (tầm 10s)

# 4. Lấy IP của ESP32 cho trang điều khiển (https://taducanh1605.github.io/espwake/)

+ bước 1: trên windows, chạy: ./check_ip.ps1 trong powerShell, tìm ra IP của esp32
+ bước 2: truy cập trang http://ip:2443 (vd: http://192.168.1.87:2443) để lấy ipv6 global
+ bước 3: copy IPv6 HTTPS ở cuối, ip global sẽ KHÔNG có phần đầu là fe80:...
+ bước 4: truy cập trang https://taducanh1605.github.io/espwake/ để điền thông tin:
	* Computer name (đặt tên cho máy của bạn)
	* ESP32 address or proxy URL (nơi đền IPv6 HTTPS)
	* New control password (điền mật khẩu từ mục 3.4)
	-> Chọn biểu tượng Save
	
# 5. Hướng dẫn dùng:

+ Trên trang web https://taducanh1605.github.io/espwake/, bạn có thể chọn nút cài đặt ở phía cuối thanh địa chỉ để sử dụng như 1 ứng dụng
+ Có thể thêm nhiều thiết bị cùng lúc
+ Góc trên bên phải của mỗi thiết bị có nút mở Setting, trong dãy các nút dưới cùng, sẽ có:
	* 1. nút Save sau khi tùy chỉnh lại setting
	* 2. nút config sẽ truy cập vào trnag config của mạch ESP32
	* 3. nút force shutdown, dùng để sập nguồn máy tính khi máy bị treo lúc khởi động
	* 4. nút xóa, dùng để xóa 1 máy ra khỏi danh sách thiết bị
	* 5. nút refresh, dùng để cập nhật lại trạng thái thiết bị (việc cập nhật là tự động sau mỗi 5-10s)
	
	
/!\ Chú ý các lỗi có thể phát sinh và cách xử lý:

- Không phát hiện được trạng thái thiết bị đã kết nối: lỗi phát sinh cho ip của thiết bị không trả lời ping. Cần vào option sharing (chạy trong run: ms-settings:network-advancedsharing), kích hoạt cho phép Network discovery và File and printer sharing.
- Không tìm thấy wifi khi scan trên esp32: với lỗi này, có thể điền SSID thủ công chính là tên wifi bạn muốn sử dụng
- Không có ipv6 global: lỗi này do việc lấy ipv6 global trên esp32 bị chậm, có thể chờ 5' sau, hoa7c5 bấm nút RST trên ESP32 để reconnect wifi lại
- Muốn setup lại: giữ nút BOOT trên ESP32 trong khoảng 15s cho đến khi LED xanh nháy nhiều lần thì nhả ra, sẽ thấy lại wifi ESP32-Wake-Setup để setup lại
- Mạch không kích nguồn máy tính: kiểm tra lại chiều âm dương khi cắm pin PWR+/- trên mainboard
- Mạch không có điện khi tắt máy, có 2 giải pháp: 1 là vào BIOS bật chế độ cấp nguồn cho cổng USB khi tắt máy, 2 là dùng 1 củ sạc bên ngoài cấp nguồn qua cổng type C
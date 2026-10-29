# Danh sách kiểm tra dự án

Nguồn: `W10_Group project.pdf` và `sws.1.pdf`.

| Yêu cầu | Trạng thái | Cách kiểm tra |
| --- | --- | --- |
| `GET`, `HEAD`, HTTP/1.0 và nhận yêu cầu HTTP/1.1 | Đã triển khai | `tests/test_sws.c` trên Ubuntu |
| `If-Modified-Since`, `Date`, `Server`, `Last-Modified`, `Content-Length` | Đã triển khai cho tệp tĩnh | `tests/test_sws.c`; cần kiểm tra thêm trên hệ tham chiếu |
| MIME qua magic pattern | Dùng lệnh `file`, có nhận dạng chữ ký dự phòng | Cần kiểm tra trực tiếp trên NetBSD/OmniOS |
| Chỉ mục thư mục và bỏ tệp ẩn | Đã triển khai | `tests/test_sws.c` |
| Thư mục người dùng `/~username` | Đã triển khai | Cần kiểm tra với tài khoản có thư mục `sws` |
| CGI dưới `/cgi-bin` khi có `-c` | Đã triển khai phần chính | `tests/test_sws.c` và `tests/cgi_fixture.c`; cần kiểm tra thêm các trường hợp RFC 3875 |
| Chạy nền và xử lý kết nối đồng thời | Đã triển khai | `tests/test_sws.c` trên Ubuntu |
| Chế độ `-d`, `-i`, `-l`, `-p`, `-h` | Đã triển khai | Đã kiểm tra trên Ubuntu, gồm IPv6 loopback và `-h` |
| Không ghi stdout khi chạy bình thường | Đã triển khai | `tests/test_sws.c` |
| Biên dịch/chạy trên NetBSD 11.0 | Chưa kiểm tra | Cần máy tham chiếu |
| Biên dịch/chạy trên OmniOS | Chưa kiểm tra | Cần máy tham chiếu |
| Đóng góp nhóm và lịch sử Git | Chưa hoàn thành | Cần username thật và lịch sử làm việc |
| Tệp tar cuối cùng | Chưa tạo | Dùng username thật của nhóm |

Hạn nộp theo đề bài: 14/12/2026 lúc 15:59:59.

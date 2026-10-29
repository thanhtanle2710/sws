# Dự án nhóm CS631: Máy chủ web đơn giản

`sws` là máy chủ web viết bằng C theo đề bài CS631 và trang hướng dẫn `sws(1)`.
Máy chủ trả lời bằng HTTP/1.0, chấp nhận yêu cầu HTTP/1.0 và HTTP/1.1,
hỗ trợ tệp tĩnh, `GET`, `HEAD` và CGI.

## Biên dịch và chạy

```sh
make
./sws -d -p 8080 ./www
```

Mở `http://localhost:8080/`; nhấn Ctrl-C để dừng. Chế độ `-d` chạy ở
terminal và in log ra stdout. Bỏ `-d` để chạy nền và xử lý nhiều kết nối
đồng thời. Cú pháp đầy đủ: `sws [-dh] [-c dir] [-i address] [-l file] [-p port] dir`.
Xem trang hướng dẫn `sws(1)` đi kèm đề bài để biết ý nghĩa từng tùy chọn.

## Mã nguồn và kiểm thử

| Phần | Tệp |
| --- | --- |
| Tham số và khởi động | `main.c` |
| Socket, daemon, kết nối đồng thời | `server.c`, `server.h` |
| Phân tích yêu cầu HTTP | `request.c`, `request.h` |
| Phản hồi HTTP và tệp tĩnh | `response.c`, `response.h` |
| CGI | `cgi.c`, `cgi.h` |
| Bài kiểm tra viết bằng C | `tests/test_sws.c`, `tests/cgi_fixture.c` |

Chạy `make test` để biên dịch và kiểm tra các phần trên. Bài kiểm tra daemon
dùng `/proc` nên chỉ chạy trên Linux. Mã nguồn còn cần được kiểm tra trực tiếp
trên NetBSD 11.0 và OmniOS theo đề bài.

## Đóng góp và phối hợp của nhóm

Trước khi nộp, nhóm cần thay các dòng dưới đây bằng **username và công việc
thực tế** của từng người. Không gán công việc cho người chưa tham gia. Lịch sử
Git hiện tại dùng tên `lethanhtan`; các thành viên khác cần tự đóng góp và
ghi nhận phần việc của mình.

| Thành viên | Phần đã làm | Cách phối hợp / kiểm tra chéo |
| --- | --- | --- |
| `lethanhtan` | Khởi tạo dự án; socket, daemon và kết nối đồng thời | Cập nhật theo thực tế trước khi nộp |
| Username thành viên 2 | Cập nhật theo thực tế | Cập nhật theo thực tế |
| Username thành viên 3 | Cập nhật theo thực tế | Cập nhật theo thực tế |
| Username thành viên 4 | Cập nhật theo thực tế | Cập nhật theo thực tế |

## Nộp bài

Nộp mã nguồn cùng `Makefile`, `README.md`, `checklist.md` và lịch sử Git.
Tên tệp tar và thư mục gốc khi giải nén phải ghép từ username thật theo mẫu
trong đề bài. Hạn nộp: **14/12/2026, 15:59:59**.

# Máy chủ web đơn giản CS631

Chương trình `sws` được viết bằng C theo đề bài nhóm CS631 và tài liệu `sws(1)`.
Máy chủ trả lời HTTP/1.0, nhận yêu cầu HTTP/1.0 hoặc HTTP/1.1, phục vụ `GET`
và `HEAD` rồi đóng kết nối.

## Biên dịch và chạy

```sh
make
./sws -d -p 8080 ./www
```

Mở `http://localhost:8080/` hoặc chạy `curl -i http://localhost:8080/`.
Tùy chọn `-d` dùng để kiểm thử: máy chủ chạy ở foreground, xử lý từng kết nối
và in log ra stdout. Bỏ `-d` để máy chủ tách khỏi terminal và xử lý nhiều kết
nối đồng thời bằng tiến trình con:

```sh
./sws -p 8080 ./www
```

Cú pháp đầy đủ:

```text
sws [-dh] [-c dir] [-i address] [-l file] [-p port] dir
```

- `-c dir`: cho phép chạy CGI trong thư mục này qua URI `/cgi-bin/...`.
- `-d`: chạy foreground, xử lý tuần tự và ghi log ra stdout.
- `-h`: in hướng dẫn ngắn.
- `-i address`: chỉ lắng nghe trên địa chỉ IPv4 hoặc IPv6 được chọn.
- `-l file`: ghi log vào tệp khi không bật `-d`.
- `-p port`: cổng lắng nghe; mặc định là 8080.
- `dir`: thư mục gốc chứa nội dung web.

Không bật `-i` thì máy chủ thử lắng nghe trên cả IPv4 và IPv6. Không bật `-l`
hoặc `-d` thì máy chủ không ghi log yêu cầu.

## Cách tổ chức mã nguồn

- `main.c`: phân tích tùy chọn và kiểm tra thư mục.
- `server.[ch]`: mở socket, chạy daemon, nhận kết nối, tạo tiến trình con và ghi log.
- `request.[ch]`: phân tích dòng yêu cầu và các header cần dùng.
- `response.[ch]`: tạo phản hồi, phục vụ tệp, chỉ mục thư mục và thư mục người dùng.
- `cgi.[ch]`: chọn và chạy script, đặt biến môi trường, xử lý đầu ra CGI.
- `www/`: nội dung web mẫu; `tests/`: kiểm tra tích hợp.

`server.c` sở hữu và đóng socket của client. `request.c` không truy cập socket
hay tệp. Bộ xử lý phản hồi trả về mã trạng thái và `Content-Length` để server
ghi log đúng định dạng; đây là lý do giao diện ban đầu được mở rộng thành
`HttpResult handle_request(int, const HttpRequest *, const ServerConfig *)`.

## Chức năng HTTP và CGI

Tệp tĩnh có các header `Date`, `Server`, `Last-Modified`, `Content-Type` và
`Content-Length`. Máy chủ thử dùng lệnh `file` để nhận dạng MIME theo magic pattern;
nếu lệnh không hỗ trợ xuất MIME, máy chủ kiểm tra chữ ký tệp thông dụng và
phần mở rộng. `If-Modified-Since` có thể trả `304 Not Modified`. `HEAD` chỉ gửi
header, không gửi body.

Nếu thư mục không có `index.html`, máy chủ tạo danh sách tên theo thứ tự chữ
và bỏ qua tên bắt đầu bằng `.`. URI `/~username/...` được ánh xạ tới thư mục
`<home của username>/sws/...`. URI `/cgi-bin/...` chỉ chạy script khi đã đặt
`-c`; CGI hỗ trợ biến môi trường cơ bản, `Status`, `Content-Type`, `Location`,
body, `PATH_INFO` và `QUERY_STRING`. Script CGI được giới hạn đầu ra ở 16 MiB.

Máy chủ không giải mã URI mã hóa phần trăm, đúng với ngoại lệ trong đề bài PDF.
Chỉ nên phục vụ nội dung và script đáng tin cậy; CGI chạy với quyền của tiến
trình máy chủ.

## Kiểm thử

```sh
make
python3 tests/smoke.py
python3 tests/daemon.py
```

`smoke.py` kiểm tra HTTP, CGI và chế độ `-d` qua socket loopback.
`daemon.py` kiểm tra chạy nền, kết nối đồng thời và log tệp; bài này dùng
`/proc` nên dành cho Linux. Cả hai đã chạy trên Ubuntu. IPv6 loopback cũng đã
được kiểm tra thủ công. Cần tiếp tục biên dịch và kiểm thử trên NetBSD 11.0 và
OmniOS; lệnh `file -i` trên OmniOS có ý nghĩa khác nên kết quả dự phòng ở đó
vẫn cần đối chiếu trực tiếp.
Makefile tự thêm `-lsocket -lnsl` khi biên dịch trên SunOS/OmniOS.

## Demo theo phần việc của từng thành viên

Các mục dưới đây là kịch bản trình bày theo **phân công dự kiến**. Chạy `make`
trước mỗi demo. Các lệnh dùng cổng 8080, vì vậy hãy dừng server của demo trước
bằng Ctrl-C trước khi bắt đầu demo tiếp theo. Người trình bày nên mở tệp mã
nguồn được ghi ở đầu mục và giải thích luồng xử lý tương ứng.

### Thành viên 1: socket, daemon và kết nối đồng thời (`server.c`, `server.h`)

1. Mở `server.c`, chỉ ra `open_listeners()`, `daemonize()`, `serve_client()` và
   `run_server()`: lần lượt mở socket IPv4/IPv6, chạy nền, đọc yêu cầu và nhận
   kết nối. `server.h` chứa cấu hình truyền vào server.
2. Trên Ubuntu, chạy `python3 tests/daemon.py`. Bài test khởi chạy daemon,
   giữ một kết nối chưa gửi xong yêu cầu, rồi xác nhận kết nối thứ hai vẫn
   nhận phản hồi. Nó cũng kiểm tra log tệp và tự dừng daemon đã tạo.
3. Để thấy log ở terminal, chạy `./sws -d -p 8080 ./www` ở terminal A và
   `curl -i http://localhost:8080/` ở terminal B. Terminal A in một dòng log;
   dừng bằng Ctrl-C.

### Thành viên 2: phân tích yêu cầu HTTP (`request.c`, `request.h`)

Chạy `./sws -d -p 8080 ./www` ở terminal A. Ở terminal B, gửi một yêu cầu
HTTP/1.1 thành hai phần rồi gửi một dòng yêu cầu sai định dạng:

```sh
python3 - <<'PY'
import socket
import time

def show_status(parts):
    with socket.create_connection(("127.0.0.1", 8080)) as client:
        for part in parts:
            client.sendall(part)
            time.sleep(0.05)
        client.shutdown(socket.SHUT_WR)
        response = client.recv(4096)
        print(response.split(b"\r\n", 1)[0].decode())

show_status([b"GET / HTTP/1.1\r\n", b"Host: localhost\r\n\r\n"])
show_status([b"bad request\r\n\r\n"])
PY
```

Kết quả mong đợi: `HTTP/1.0 200 OK` và `HTTP/1.0 400 Bad Request`. Giải thích
`parse_request()` chỉ phân tích dữ liệu đã nhận; `server.c` chịu trách nhiệm
đọc từ socket cho tới hết header. Dừng server ở terminal A bằng Ctrl-C.

### Thành viên 3: phản hồi và tệp tĩnh (`response.c`, `response.h`)

Chạy `./sws -d -p 8080 ./www` ở terminal A. Ở terminal B:

```sh
curl -i http://localhost:8080/
curl -I http://localhost:8080/
curl -i http://localhost:8080/khong-ton-tai
modified=$(curl -sI http://localhost:8080/ | sed -n 's/^Last-Modified: //p' | tr -d '\r')
curl -i -H "If-Modified-Since: $modified" http://localhost:8080/
mkdir -p www/demo-list
touch www/demo-list/b.txt www/demo-list/a.txt www/demo-list/.hidden
curl -i http://localhost:8080/demo-list/
rm -r www/demo-list
```

Chỉ ra `GET` có body, `HEAD` không có body, tệp thiếu trả `404`, yêu cầu có
`If-Modified-Since` trả `304`, và chỉ mục thư mục sắp `a.txt` trước `b.txt`
nhưng không liệt kê `.hidden`. Giải thích `response.c` chọn tài nguyên và tạo
header HTTP. Dừng server ở terminal A bằng Ctrl-C.

### Thành viên 4: CGI và kiểm thử (`cgi.c`, `cgi.h`, `tests/`)

Ở terminal A, tạo script CGI tạm rồi chạy server:

```sh
CGI_DIR=$(mktemp -d)
cat > "$CGI_DIR/hello" <<'SH'
#!/bin/sh
printf 'Content-Type: text/plain\n\n'
printf 'method=%s query=%s path=%s\n' "$REQUEST_METHOD" "$QUERY_STRING" "$PATH_INFO"
SH
chmod +x "$CGI_DIR/hello"
./sws -d -p 8080 -c "$CGI_DIR" ./www
```

Ở terminal B, chạy:

```sh
curl -i 'http://localhost:8080/cgi-bin/hello/part?x=1'
```

Kết quả có `method=GET query=x=1 path=/part`. Giải thích `cgi.c` chọn script,
đặt biến môi trường, chạy tiến trình con và chuyển header/body của script
thành phản hồi HTTP. Dừng server bằng Ctrl-C; ở terminal A chạy
`rm -r "$CGI_DIR"` để xóa script tạm. Sau đó chạy `python3 tests/smoke.py`
để trình bày các ca kiểm thử CGI và HTTP.

## Phân công và nộp bài

Đề xuất chia việc thành bốn phần: socket/daemon, phân tích yêu cầu,
phản hồi/tệp tĩnh và CGI/kiểm thử. Trước khi nộp, ghi tại đây công việc thực
tế của từng thành viên và cách phối hợp. Bản nộp cần có lịch sử Git.
Tên tệp tar và thư mục gốc khi giải nén phải dùng username thật của nhóm;
ví dụ trong PDF có ba username, còn số lượng tên thực tế phải khớp với nhóm.

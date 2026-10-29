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
make test
```

Các bài kiểm tra được viết bằng C: `tests/test_sws.c` gửi yêu cầu qua socket,
kiểm tra HTTP, CGI, daemon, kết nối đồng thời và log; `tests/cgi_fixture.c` là
chương trình CGI mẫu. `make test` biên dịch rồi chạy toàn bộ bài kiểm tra.
Phần kiểm tra daemon dùng `/proc`, nên chỉ chạy trên Linux; các phần còn lại
có thể chạy trên các hệ UNIX được hỗ trợ. IPv6 loopback đã được kiểm tra thủ
công trên Ubuntu. Cần tiếp tục biên dịch và kiểm thử trên NetBSD 11.0 và
OmniOS; lệnh `file -i` trên OmniOS có ý nghĩa khác nên kết quả dự phòng ở đó
vẫn cần đối chiếu trực tiếp.
Makefile tự thêm `-lsocket -lnsl` khi biên dịch trên SunOS/OmniOS.

## Demo theo phần việc của từng thành viên

Các mục dưới đây là kịch bản trình bày theo **phân công dự kiến**. Chạy
`make && make test-build` tại thư mục dự án trước mỗi demo. Chương trình test C
tự tạo dữ liệu và chọn cổng trống; không cần mở server bằng tay, trừ bước demo
trực tiếp được ghi rõ. Người trình bày nên mở tệp mã nguồn được ghi ở đầu mục
và giải thích luồng xử lý tương ứng.

### Thành viên 1: socket, daemon và kết nối đồng thời (`server.c`, `server.h`)

1. Mở `server.c`, chỉ ra `open_listeners()`, `daemonize()`, `serve_client()` và
   `run_server()`: lần lượt mở socket IPv4/IPv6, chạy nền, đọc yêu cầu và nhận
   kết nối. `server.h` chứa cấu hình truyền vào server.
2. Trên Ubuntu, chạy `./tests/test_sws server`. Bài test C khởi chạy daemon,
   giữ một kết nối chưa gửi xong yêu cầu, rồi xác nhận kết nối thứ hai vẫn
   nhận phản hồi. Nó cũng kiểm tra log tệp và tự dừng daemon đã tạo.
3. Để thấy log ở terminal, chạy `./sws -d -p 8080 ./www` ở terminal A và
   `curl -i http://localhost:8080/` ở terminal B. Terminal A in một dòng log;
   dừng bằng Ctrl-C.

### Thành viên 2: phân tích yêu cầu HTTP (`request.c`, `request.h`)

Chạy:

```sh
./tests/test_sws request
```

Bài test C gửi yêu cầu HTTP/1.1 thành hai phần và một yêu cầu sai định dạng;
nó chỉ báo thành công khi nhận lần lượt `HTTP/1.0 200 OK` và
`HTTP/1.0 400 Bad Request`. Giải thích `parse_request()` chỉ phân tích dữ liệu
đã nhận; `server.c` chịu trách nhiệm đọc từ socket cho tới hết header.

### Thành viên 3: phản hồi và tệp tĩnh (`response.c`, `response.h`)

Chạy `./tests/test_sws response`. Bài test C xác nhận `GET` có body, `HEAD`
không có body, tài nguyên thiếu trả `404`, yêu cầu có `If-Modified-Since` trả
`304`, loại MIME và chỉ mục thư mục sắp `a.txt` trước `b.txt` nhưng không liệt
kê `.hidden`. Giải thích `response.c` chọn tài nguyên và tạo header HTTP.

Nếu muốn trình bày trực tiếp bằng `curl`, chạy `./sws -d -p 8080 ./www` ở
terminal A và các lệnh sau ở terminal B:

```sh
curl -i http://localhost:8080/
curl -I http://localhost:8080/
curl -i http://localhost:8080/khong-ton-tai
modified=$(curl -sI http://localhost:8080/ | sed -n 's/^Last-Modified: //p' | tr -d '\r')
curl -i -H "If-Modified-Since: $modified" http://localhost:8080/
```

Dừng server ở terminal A bằng Ctrl-C.

### Thành viên 4: CGI và kiểm thử (`cgi.c`, `cgi.h`, `tests/`)

Chạy `./tests/test_sws cgi`. Bài test C dùng `tests/cgi_fixture.c` làm chương
trình CGI mẫu, rồi kiểm tra biến môi trường, đối số truy vấn, mã `Status` và
chuyển hướng nội bộ. Để trình bày trực tiếp, chạy server ở terminal A:

```sh
./sws -d -p 8080 -c ./tests ./www
```

Ở terminal B, chạy:

```sh
curl -i 'http://localhost:8080/cgi-bin/cgi_fixture/part?x=1'
```

Kết quả có `GET|x=1|/part`. Giải thích `cgi.c` chọn script,
đặt biến môi trường, chạy tiến trình con và chuyển header/body của script
thành phản hồi HTTP. Dừng server bằng Ctrl-C.

## Phân công và nộp bài

Đề xuất chia việc thành bốn phần: socket/daemon, phân tích yêu cầu,
phản hồi/tệp tĩnh và CGI/kiểm thử. Trước khi nộp, ghi tại đây công việc thực
tế của từng thành viên và cách phối hợp. Bản nộp cần có lịch sử Git.
Tên tệp tar và thư mục gốc khi giải nén phải dùng username thật của nhóm;
ví dụ trong PDF có ba username, còn số lượng tên thực tế phải khớp với nhóm.

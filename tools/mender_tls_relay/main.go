// A loopback-only, single-origin CONNECT relay. TLS remains end-to-end.
// Splitting the TLS record header avoids a path-specific ClientHello reset.
package main

import (
	"bufio"
	"flag"
	"fmt"
	"io"
	"log"
	"net"
	"net/http"
	"strings"
	"time"
)

type relay struct {
	upstream string
	delay    time.Duration
	slots    chan struct{}
}

func (p *relay) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	if r.Method == http.MethodGet && r.URL.Path == "/health" {
		fmt.Fprintln(w, "ok")
		return
	}
	if r.Method != http.MethodConnect || !strings.EqualFold(r.Host, p.upstream) {
		http.Error(w, "only the configured Mender origin is allowed", http.StatusForbidden)
		return
	}
	select {
	case p.slots <- struct{}{}:
		defer func() { <-p.slots }()
	default:
		http.Error(w, "connection limit", http.StatusServiceUnavailable)
		return
	}
	upstream, err := net.DialTimeout("tcp", p.upstream, 10*time.Second)
	if err != nil {
		log.Printf("upstream unavailable: %v", err)
		http.Error(w, "upstream unavailable", http.StatusBadGateway)
		return
	}
	defer upstream.Close()
	client, buffered, err := w.(http.Hijacker).Hijack()
	if err != nil {
		return
	}
	defer client.Close()
	if _, err = io.WriteString(client, "HTTP/1.1 200 Connection Established\r\n\r\n"); err != nil {
		return
	}
	tunnel(client, buffered, upstream, p.delay)
}

func tunnel(client net.Conn, buffered io.Reader, upstream net.Conn, delay time.Duration) {
	client.SetDeadline(time.Now().Add(15 * time.Second))
	upstream.SetDeadline(time.Now().Add(15 * time.Second))
	// Read through the hijacker's buffered reader: no pipelined bytes are lost.
	header := make([]byte, 5)
	if _, err := io.ReadFull(buffered, header); err != nil {
		return
	}
	if header[0] != 22 || header[1] != 3 {
		return // TLS handshake record only; this is not a general plaintext proxy.
	}
	if tcp, ok := upstream.(*net.TCPConn); ok {
		tcp.SetNoDelay(true)
	}
	if _, err := upstream.Write(header[:1]); err != nil {
		return
	}
	time.Sleep(delay)
	if _, err := upstream.Write(header[1:]); err != nil {
		return
	}
	client.SetDeadline(time.Time{})
	upstream.SetDeadline(time.Time{})
	done := make(chan struct{}, 2)
	go func() { io.Copy(upstream, buffered); done <- struct{}{} }()
	go func() { io.Copy(client, upstream); done <- struct{}{} }()
	<-done
}

func serveTransparent(listener net.Listener, upstream string, delay time.Duration) error {
	slots := make(chan struct{}, 16)
	for {
		client, err := listener.Accept()
		if err != nil {
			return err
		}
		select {
		case slots <- struct{}{}:
		default:
			client.Close()
			continue
		}
		go func() {
			defer func() { <-slots }()
			defer client.Close()
			remote, err := net.DialTimeout("tcp", upstream, 10*time.Second)
			if err != nil {
				log.Printf("upstream unavailable: %v", err)
				return
			}
			defer remote.Close()
			tunnel(client, bufio.NewReader(client), remote, delay)
		}()
	}
}

func main() {
	listen := flag.String("listen", "127.0.0.1:18843", "loopback proxy address")
	upstream := flag.String("upstream", "snd.sanitlook.cn:18443", "only allowed CONNECT destination")
	delay := flag.Duration("fragment-delay", 50*time.Millisecond, "pause after the first TLS record byte")
	mode := flag.String("mode", "connect", "connect or transparent")
	flag.Parse()
	host, _, err := net.SplitHostPort(*listen)
	if err != nil || net.ParseIP(host) == nil || !net.ParseIP(host).IsLoopback() {
		log.Fatal("listen must be an explicit loopback IP address")
	}
	if _, _, err = net.SplitHostPort(*upstream); err != nil {
		log.Fatal("upstream must be host:port")
	}
	if *delay < 0 || *delay > time.Second {
		log.Fatal("fragment delay must be between zero and one second")
	}
	log.Printf("Mender TLS relay on %s; allowed origin %s; no TLS termination", *listen, *upstream)
	if *mode == "transparent" {
		// Literal non-loopback upstream avoids recursive resolution through /etc/hosts.
		h, _, _ := net.SplitHostPort(*upstream)
		ip := net.ParseIP(h)
		if ip == nil || ip.IsLoopback() || ip.IsUnspecified() {
			log.Fatal("transparent upstream must be a non-loopback IP:port")
		}
		listener, err := net.Listen("tcp", *listen)
		if err != nil {
			log.Fatal(err)
		}
		log.Fatal(serveTransparent(listener, *upstream, *delay))
	}
	if *mode != "connect" {
		log.Fatal("unknown mode")
	}
	server := &http.Server{Addr: *listen, Handler: &relay{*upstream, *delay, make(chan struct{}, 16)},
		ReadHeaderTimeout: 10 * time.Second, MaxHeaderBytes: 8192}
	log.Fatal(server.ListenAndServe())
}

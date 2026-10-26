package main

import (
	"bytes"
	"crypto/sha256"
	"crypto/tls"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"net/url"
	"testing"
	"time"
)

func TestTransparentTLSStreaming(t *testing.T) {
	payload := bytes.Repeat([]byte("signed artifact bytes"), 65536)
	upstream := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		io.Copy(w, r.Body)
	}))
	defer upstream.Close()
	u, _ := url.Parse(upstream.URL)
	listener, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	defer listener.Close()
	go serveTransparent(listener, u.Host, time.Millisecond)
	transport := &http.Transport{TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 10 * time.Second}
	response, err := client.Post("https://"+listener.Addr().String(), "application/octet-stream", bytes.NewReader(payload))
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	data, err := io.ReadAll(response.Body)
	if err != nil || !bytes.Equal(data, payload) {
		t.Fatal("transparent payload changed", err)
	}
	if !bytes.Equal(response.TLS.PeerCertificates[0].Raw, upstream.Certificate().Raw) {
		t.Fatal("upstream certificate changed")
	}
}

func TestOnlyConfiguredOriginAllowed(t *testing.T) {
	p := &relay{"snd.sanitlook.cn:18443", time.Millisecond, make(chan struct{}, 2)}
	for _, target := range []string{"other.example:443", "snd.sanitlook.cn:22"} {
		r := httptest.NewRequest("CONNECT", "http://"+target, nil)
		w := httptest.NewRecorder()
		p.ServeHTTP(w, r)
		if w.Code != http.StatusForbidden {
			t.Fatalf("unexpected target permitted: %s", target)
		}
	}
}

func TestEndToEndTLSAndStreamingUploadDownload(t *testing.T) {
	payload := bytes.Repeat([]byte("Mender encrypted payload\x00"), 65536)
	expected := sha256.Sum256(payload)
	upstream := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		data, err := io.ReadAll(r.Body)
		if err != nil || sha256.Sum256(data) != expected {
			t.Error("upload changed while crossing relay")
		}
		w.Write(data)
	}))
	defer upstream.Close()
	u, _ := url.Parse(upstream.URL)
	proxy := httptest.NewServer(&relay{u.Host, 10 * time.Millisecond, make(chan struct{}, 2)})
	defer proxy.Close()
	proxyURL, _ := url.Parse(proxy.URL)
	transport := &http.Transport{Proxy: http.ProxyURL(proxyURL),
		TLSClientConfig: &tls.Config{InsecureSkipVerify: true}}
	defer transport.CloseIdleConnections()
	client := &http.Client{Transport: transport, Timeout: 10 * time.Second}
	response, err := client.Post(upstream.URL, "application/octet-stream", bytes.NewReader(payload))
	if err != nil {
		t.Fatal(err)
	}
	defer response.Body.Close()
	data, err := io.ReadAll(response.Body)
	if err != nil || response.StatusCode != 200 || sha256.Sum256(data) != expected {
		t.Fatal("download changed while crossing relay", err)
	}
	// The peer certificate belongs to upstream, proving no TLS termination here.
	if !bytes.Equal(response.TLS.PeerCertificates[0].Raw, upstream.Certificate().Raw) {
		t.Fatal("relay replaced the upstream certificate")
	}
}

package ws

import (
	"encoding/json"
	"log"
	"net/http"
	"sync"
	"time"

	"github.com/gorilla/websocket"
)

var Upgrader = websocket.Upgrader{CheckOrigin: func(r *http.Request) bool { return true }}

const (
	sendQueueSize = 32
	writeTimeout  = 5 * time.Second
	pongWait      = 60 * time.Second
	pingPeriod    = (pongWait * 9) / 10
)

type Client struct {
	conn *websocket.Conn
	send chan []byte
}

type Server struct {
	Mu    sync.RWMutex
	Conns map[*Client]bool
	srv   *http.Server
	wg    sync.WaitGroup
}

func NewServer() *Server {
	return &Server{Conns: make(map[*Client]bool)}
}

func (s *Server) Start(addr string) error {
	mux := http.NewServeMux()
	mux.HandleFunc("/ws", s.handleWebSocket)

	s.srv = &http.Server{Addr: addr, Handler: mux}
	go func() {
		if err := s.srv.ListenAndServe(); err != nil && err != http.ErrServerClosed {
			log.Printf("WebSocket server error: %v", err)
		}
	}()
	return nil
}

func (s *Server) handleWebSocket(w http.ResponseWriter, r *http.Request) {
	conn, err := Upgrader.Upgrade(w, r, nil)
	if err != nil {
		log.Printf("WebSocket upgrade error: %v", err)
		return
	}

	client := &Client{conn: conn, send: make(chan []byte, sendQueueSize)}

	s.Mu.Lock()
	s.Conns[client] = true
	clientCount := len(s.Conns)
	s.Mu.Unlock()
	log.Printf("WebSocket client connected. Total clients: %d", clientCount)

	s.wg.Add(1)
	go func() { defer s.wg.Done(); s.clientWriter(client) }()

	conn.SetReadLimit(512)
	conn.SetReadDeadline(time.Now().Add(pongWait))
	conn.SetPongHandler(func(string) error { conn.SetReadDeadline(time.Now().Add(pongWait)); return nil })

	for {
		_, _, err := conn.ReadMessage()
		if err != nil {
			if websocket.IsUnexpectedCloseError(err, websocket.CloseGoingAway, websocket.CloseAbnormalClosure) {
				log.Printf("WebSocket error: %v", err)
			}
			break
		}
	}
	s.removeClient(client)
}

func (s *Server) clientWriter(c *Client) {
	ticker := time.NewTicker(pingPeriod)
	defer func() {
		ticker.Stop()
		c.conn.Close()
	}()

	for {
		select {
		case msg, ok := <-c.send:
			c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if !ok {
				c.conn.WriteMessage(websocket.CloseMessage, []byte{})
				return
			}
			if err := c.conn.WriteMessage(websocket.TextMessage, msg); err != nil {
				log.Printf("Error writing to WebSocket client: %v", err)
				return
			}
		case <-ticker.C:
			c.conn.SetWriteDeadline(time.Now().Add(writeTimeout))
			if err := c.conn.WriteMessage(websocket.PingMessage, nil); err != nil {
				log.Printf("Error pinging WebSocket client: %v", err)
				return
			}
		}
	}
}

func (s *Server) removeClient(c *Client) {
	s.Mu.Lock()
	if _, ok := s.Conns[c]; ok {
		delete(s.Conns, c)
	}
	s.Mu.Unlock()
	close(c.send)
	log.Println("WebSocket client disconnected")
}

func (s *Server) Broadcast(v interface{}) {
	data, err := json.Marshal(v)
	if err != nil {
		log.Printf("Error marshaling broadcast message: %v", err)
		return
	}
	s.BroadcastBytes(data)
}

func (s *Server) BroadcastBytes(data []byte) {
	s.Mu.RLock()
	clients := make([]*Client, 0, len(s.Conns))
	for c := range s.Conns {
		clients = append(clients, c)
	}
	s.Mu.RUnlock()

	if len(clients) == 0 {
		return
	}

	for _, c := range clients {
		select {
		case c.send <- data:
		default:
			log.Printf("Dropping slow WebSocket client")
			s.removeClient(c)
		}
	}
}

func (s *Server) Shutdown() error {
	if s.srv != nil {
		_ = s.srv.Close()
	}
	s.Mu.Lock()
	for c := range s.Conns {
		close(c.send)
	}
	s.Mu.Unlock()
	s.wg.Wait()
	return nil
}

func (s *Server) ClientCount() int {
	s.Mu.RLock()
	defer s.Mu.RUnlock()
	return len(s.Conns)
}

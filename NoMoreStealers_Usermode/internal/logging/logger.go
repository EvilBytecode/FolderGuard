package logging

import (
	"bufio"
	"fmt"
	"os"
	"path/filepath"
	"sync"
	"time"

	lumberjack "gopkg.in/natefinch/lumberjack.v2"
)

type Logger struct {
	out   *lumberjack.Logger
	ch    chan string
	wg    sync.WaitGroup
	buf   *bufio.Writer
	mu    sync.Mutex
	alive bool
}

func NewLogger(fpath string) (*Logger, error) {
	dir := filepath.Dir(fpath)
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return nil, fmt.Errorf("failed to create log directory: %w", err)
	}

	lj := &lumberjack.Logger{
		Filename:   fpath,
		MaxSize:    10, 
		MaxBackups: 5,
		MaxAge:     28,  
		Compress:   true, 
	}

	l := &Logger{
		out:   lj,
		ch:    make(chan string, 1024),
		buf:   bufio.NewWriter(lj),
		alive: true,
	}
	l.wg.Add(1)
	go l.loop()
	return l, nil
}

func (l *Logger) loop() {
	defer l.wg.Done()
	flushTicker := time.NewTicker(2 * time.Second)
	defer flushTicker.Stop()
	for {
		select {
		case line, ok := <-l.ch:
			if !ok {
				l.mu.Lock()
				l.buf.Flush()
				l.mu.Unlock()
				return
			}
			l.mu.Lock()
			l.buf.WriteString(line)
			l.buf.WriteByte('\n')
			l.mu.Unlock()
		case <-flushTicker.C:
			l.mu.Lock()
			l.buf.Flush()
			l.mu.Unlock()
		}
	}
}

func (l *Logger) Log(line string) {
	if l == nil {
		return
	}
	select {
	case l.ch <- line:
	default:
	}
}

func (l *Logger) Shutdown() error {
	if l == nil {
		return nil
	}
	if !l.alive {
		return nil
	}
	l.alive = false
	close(l.ch)
	l.wg.Wait()
	l.mu.Lock()
	_ = l.buf.Flush()
	l.mu.Unlock()
	if closer, ok := any(l.out).(interface{ Close() error }); ok {
		return closer.Close()
	}
	return nil
}

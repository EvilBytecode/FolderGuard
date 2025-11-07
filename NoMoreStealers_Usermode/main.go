package main

import (
	apppkg "NoMoreStealers/internal/app"
	"NoMoreStealers/internal/tray"
	"github.com/wailsapp/wails/v2"
	"github.com/wailsapp/wails/v2/pkg/options"
	"github.com/wailsapp/wails/v2/pkg/options/assetserver"
)

func main() {
	app := apppkg.New()

	// Connect tray click → reopen GUI
	tray.SetTrayCallbacks(func() {
		app.ShowMainWindow()
	}, func() {
		app.Quit()
	})

	err := wails.Run(&options.App{
		Title:            "NoMoreStealers",
		Width:            1500,
		Height:           950,
		AssetServer:      &assetserver.Options{Assets: AssetFS()},
		BackgroundColour: &options.RGBA{R: 27, G: 27, B: 38, A: 255},
		OnStartup:        app.OnStartup,
		OnDomReady:       app.OnDomReady,
		OnBeforeClose:    app.OnBeforeClose,
		Bind:             []interface{}{app},
	})

	if err != nil {
		println("Error:", err.Error())
	}
}

package main

import "embed"

//go:embed frontend/*
var Assets embed.FS

func AssetFS() embed.FS {
	return Assets
}

# MegaMix Shader Injector

A plugin for Hatsune Miku: Project DIVA MegaMix+ which enables loading modified shaders without replacing base-game files.

This works by taking the hash of each shader at load time and checking for an entry in the config.toml for the shader being loaded,
Formatted as shader_SHDRHASHGOESHERE = "file_name.cso", with shaders being loaded from shader/cso/file_name.cso within the same path as the dll.

Credits to [Skyth](https://github.com/blueskythlikesclouds/) for DivaModLoader.
Import("env")

# PlatformIO disables ESP-IDF's component manager by default. Codex Buddy uses
# it to resolve the official CoreS3 board support package declared by main.
env["ENV"]["IDF_COMPONENT_MANAGER"] = "1"

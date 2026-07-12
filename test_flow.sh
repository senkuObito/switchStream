#!/bin/bash
echo "Removing old screenshots and addons config..."
rm -f *.png
rm -f ./switchstream_data/addons.json

echo "Starting switchstream_sim in the background..."
./switchstream_sim > simulator.log 2>&1 &
SIM_PID=$!

echo "Waiting for SwitchStream window to appear..."
WID=""
for i in {1..30}; do
    WID=$(xdotool search --name "SwitchStream" | head -n 1)
    if [ -n "$WID" ]; then
        break
    fi
    sleep 0.5
done

if [ -z "$WID" ]; then
    echo "Error: SwitchStream window not found after 15 seconds!"
    kill $SIM_PID
    exit 1
fi

echo "Found window ID: $WID. Positioning and focusing..."
xdotool windowmove "$WID" 500 100
sleep 0.5
xdotool windowraise "$WID"
sleep 0.5

echo "1. Navigating to Addons Page..."
xdotool mousemove 868 380 click 1
sleep 0.5
xdotool key a
sleep 2.5
scrot addons_screen.png

echo "2. Switching to Discover Addons Pane (Right)..."
xdotool mousemove 868 380 click 1
sleep 0.5
xdotool key Right
sleep 2.5
scrot addons_right.png

echo "3. Scrolling down to MediaFusion addon..."
xdotool mousemove 868 380 click 1
sleep 0.5
xdotool key Down
sleep 1.0
scrot addons_down.png

echo "4. Installing MediaFusion Addon (A/Enter)..."
xdotool mousemove 868 380 click 1
sleep 0.5
xdotool key Return
sleep 6.0
scrot addons_installed.png

echo "5. Switching back to Installed Pane (Left)..."
xdotool mousemove 868 380 click 1
sleep 0.5
xdotool key Left
sleep 2.5
scrot addons_left.png

echo "6. Returning to Home screen..."
xdotool mousemove 868 380 click 1
sleep 0.5
xdotool key Escape
sleep 10.0
scrot home_screen.png

echo "Terminating simulator..."
kill $SIM_PID
sleep 1.0

echo "Test sequence finished successfully!"

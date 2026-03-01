import streamlit as st
import paho.mqtt.client as mqtt
from streamlit_autorefresh import st_autorefresh

NUM_LEDS = 12
PUBLISH_TOPIC = "lamp/control"
SUBSCRIBE_TOPIC = "lamp/data"

# Predefined colors for Timer Mode
TIMER_PALETTE = {
    "Red": [255,0,0],
    "Green": [0,255,0],
    "Blue": [0,0,255],
    "Yellow": [255,255,0],
    "Magenta": [255,0,255],
    "Cyan": [0,255,255]
}

# MQTT Settings
MQTT_BROKER = "s0169691.ala.asia-southeast1.emqxsl.com"
MQTT_PORT = 8883
MQTT_USER = "Neki"
MQTT_PASSWORD = "123499"

# ===== HELPERS =====
def send_led_update(client, leds, brightness=255):
    payload = bytearray()
    for i in range(NUM_LEDS):
        r, g, b = leds[i]
        payload.append(r)
        payload.append(g)
        payload.append(b)
        payload.append(brightness if i == 0 else 0)
    client.publish(PUBLISH_TOPIC, payload)

def send_timer(client, minutes):
    high = (minutes >> 8) & 0xFF
    low = minutes & 0xFF
    payload = bytearray([0xF0, high, low] + [0]*(NUM_LEDS*4 - 3))
    client.publish(PUBLISH_TOPIC, payload)

def validate_timer_colors(leds):
    valid_count = sum([led in TIMER_PALETTE.values() for led in leds])
    return valid_count >= 5

# ===== MQTT SETUP =====
if "mqtt_client" not in st.session_state:
    client = mqtt.Client("StreamlitController")
    client.username_pw_set(MQTT_USER, MQTT_PASSWORD)
    client.tls_set()
    st.session_state["mqtt_client"] = client
    st.session_state["mqtt_data"] = {}
    st.session_state["led_states"] = [[0,0,0] for _ in range(NUM_LEDS)]
    st.session_state["led_brightness"] = 255
    st.session_state["lamp_timer"] = 10
    st.session_state["timer_mode_adjust_all"] = True  # default

def on_connect(client, userdata, flags, rc):
    if rc == 0:
        st.session_state["mqtt_client"].subscribe(SUBSCRIBE_TOPIC)
        print("✅ MQTT Connected")
    else:
        print(f"❌ MQTT Connect failed: {rc}")

def on_message(client, userdata, msg):
    try:
        payload = msg.payload.decode()
        st.session_state["mqtt_data"] = {"last_msg": payload}
    except Exception as e:
        print(f"❌ MQTT message error: {e}")

client = st.session_state["mqtt_client"]
client.on_connect = on_connect
client.on_message = on_message
if not client.is_connected():
    client.connect(MQTT_BROKER, MQTT_PORT, 60)
client.loop(timeout=0.1)

st_autorefresh(interval=2000, key="refresh")

# ===== PAGE =====
st.title("💡 NeoLamp")

data = st.session_state.get("mqtt_data", {})
if data:
    st.success(f"✅ MQTT Connected. Last message: {data.get('last_msg','')}")
else:
    st.warning("⏳ Waiting for MQTT data...")

mode = st.radio("Select Mode", ["Lamp Mode", "Timer Mode"], key="mode_radio")
led_states = st.session_state["led_states"]
updated = False

# ===== LAMP MODE =====
if mode == "Lamp Mode":
    st.subheader("Adjust LED individually")
    led_choice = st.selectbox(
        "Select LED to adjust",
        options=[f"LED {i+1}" for i in range(NUM_LEDS)],
        key="lamp_led_select"
    )
    led_index = int(led_choice.split()[1])-1
    r = st.slider("R", 0, 255, value=led_states[led_index][0], key=f"lamp_r{led_index}")
    g = st.slider("G", 0, 255, value=led_states[led_index][1], key=f"lamp_g{led_index}")
    b = st.slider("B", 0, 255, value=led_states[led_index][2], key=f"lamp_b{led_index}")
    if [r,g,b] != led_states[led_index]:
        led_states[led_index] = [r,g,b]
        updated = True

# ===== TIMER MODE =====
else:
    st.subheader("Timer Mode LED Selection")
    st.radio(
        "Timer Mode Adjustment",
        ["Adjust All LEDs", "Adjust Each LED"],
        index=0 if st.session_state["timer_mode_adjust_all"] else 1,
        key="timer_adjust_radio",
        on_change=lambda: st.session_state.update({"timer_mode_adjust_all": st.session_state["timer_adjust_radio"] == "Adjust All LEDs"})
    )

    if st.session_state["timer_mode_adjust_all"]:
        color_choice = st.selectbox(
            "Select color for all LEDs",
            options=list(TIMER_PALETTE.keys()),
            key="timer_all_led_choice"
        )
        rgb = TIMER_PALETTE[color_choice]
        for i in range(NUM_LEDS):
            if led_states[i] != rgb:
                led_states[i] = rgb
                updated = True
    else:
        cols = st.columns(NUM_LEDS)
        for i in range(NUM_LEDS):
            with cols[i]:
                choice = st.selectbox(
                    f"LED {i+1}",
                    options=list(TIMER_PALETTE.keys()),
                    index=[v==led_states[i] for v in TIMER_PALETTE.values()].index(True)
                    if led_states[i] in TIMER_PALETTE.values() else 0,
                    key=f"timer_led_{i}"
                )
                rgb = TIMER_PALETTE[choice]
                if rgb != led_states[i]:
                    led_states[i] = rgb
                    updated = True

# ===== BRIGHTNESS =====
brightness = st.slider("Brightness", 0, 255, value=st.session_state["led_brightness"], key="brightness_slider")
if brightness != st.session_state["led_brightness"]:
    st.session_state["led_brightness"] = brightness
    updated = True

if updated:
    send_led_update(client, led_states, brightness)
    st.info("💡 LED colors updated")

# ===== TIMER SETTINGS =====
if mode == "Timer Mode":
    st.subheader("Timer Settings")
    timer_valid = validate_timer_colors(led_states)
    if not timer_valid:
        st.warning("⚠️ Timer Mode requires at least 5 LEDs set to system-defined colors!")
    timer_minutes = st.number_input(
        "Timer Duration (minutes)", min_value=1, max_value=180,
        value=st.session_state["lamp_timer"], step=1, disabled=not timer_valid,
        key="timer_minutes_input"
    )
    st.session_state["lamp_timer"] = timer_minutes

# ===== ACTION BUTTONS =====
col1, col2, col3 = st.columns(3)
with col1:
    if st.button("Turn ON Lamp", key="btn_lamp_on"):
        send_led_update(client, led_states, brightness)
        st.info(f"▶️ Lamp ON command sent")
with col2:
    if st.button("Turn OFF Lamp", key="btn_lamp_off"):
        client.publish(PUBLISH_TOPIC, bytearray([0]*NUM_LEDS*4))
        st.info("⏹ Lamp OFF command sent")
with col3:
    if st.button("Start Timer", key="btn_timer_start", disabled=(mode!="Timer Mode" or not timer_valid)):
        # Turn off all LEDs first, then light all LEDs in ESP32 timer sequence
        client.publish(PUBLISH_TOPIC, bytearray([0]*NUM_LEDS*4))
        send_led_update(client, led_states, brightness)
        send_timer(client, timer_minutes)
        st.success(f"⏱ Timer started for {timer_minutes} minutes")


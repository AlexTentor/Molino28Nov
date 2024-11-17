import serial
import time
import mido
import glob
import threading
from queue import Queue
from collections import defaultdict

class MIDIController:
    def __init__(self):
        self.setup_midi()
        self.serial = self.connect_to_esp32()
        
        # For CC control
        self.motor_speeds = defaultdict(int)
        
        # For relay control
        self.message_queue = Queue()
        self.active_notes = defaultdict(bool)
        self.serial_lock = threading.Lock()
        
        print("Controller initialized and ready!")
        print("Listening for:")
        print("- CC 1-10 for PWM control")
        print("- MIDI notes for relay triggers")
    
    def setup_midi(self):
        input_ports = mido.get_input_names()
        print("Available MIDI ports:", input_ports)
        
        iac_port = 'from Max 1'  # Change this to match your MIDI input port
        if iac_port not in input_ports:
            raise RuntimeError(f"MIDI port '{iac_port}' not found. Available ports: {input_ports}")
        
        self.midi_in = mido.open_input(iac_port)
        print(f"Connected to MIDI port: {iac_port}")

    def connect_to_esp32(self):
        patterns = ['/dev/cu.usbserial*', '/dev/cu.SLAB*', '/dev/cu.wchusbserial*']
        
        for pattern in patterns:
            ports = glob.glob(pattern)
            if ports:
                try:
                    serial_connection = serial.Serial(ports[0], 115200, timeout=1)
                    print(f"Connected to ESP32 on port: {ports[0]}")
                    time.sleep(2)  # Wait for ESP32 to initialize
                    return serial_connection
                except serial.SerialException as e:
                    print(f"Error connecting to {ports[0]}: {e}")
        
        raise RuntimeError("No ESP32 found. Check connection and try again.")

    def process_midi_message(self, message):
        if message.type == 'control_change':
            if 1 <= message.control <= 10:  # Process CC 1-10
                motor_number = message.control
                speed = message.value  # MIDI value (0-127)
                
                # Only send if value changed
                if self.motor_speeds[motor_number] != speed:
                    self.motor_speeds[motor_number] = speed
                    command = f"m{motor_number} {speed}\n"
                    print(f"Broadcasting: CC{motor_number} = {speed}")
                    with self.serial_lock:
                        self.serial.write(command.encode())
                    
        elif message.type == 'note_on':
            relay_number = message.note % 16  # Map to 16 relays
            # Add to queue with velocity to distinguish between note on/off
            self.message_queue.put((relay_number, message.velocity > 0))
            self.active_notes[message.note] = message.velocity > 0

    def send_relay_command(self, relay_mask):
        """Send command for relay mask"""
        with self.serial_lock:
            command = f"n{relay_mask:04X}\n"  # Format as 4-digit hex
            self.serial.write(command.encode())
            print(f"Activated relays: {bin(relay_mask)[2:]:016b}")  # Print binary pattern

    def process_message_queue(self):
        """Process all pending messages in the queue"""
        relay_mask = 0
        
        # Collect all pending messages and build mask
        while not self.message_queue.empty():
            relay_number, is_on = self.message_queue.get()
            if is_on:
                relay_mask |= (1 << relay_number)
        
        # Send mask if any relays need to be triggered
        if relay_mask > 0:
            self.send_relay_command(relay_mask)

    def midi_listener(self):
        """Thread function to listen for MIDI messages"""
        while True:
            for message in self.midi_in.iter_pending():
                self.process_midi_message(message)
            time.sleep(0.0001)

    def queue_processor(self):
        """Thread function to process the message queue"""
        while True:
            if not self.message_queue.empty():
                self.process_message_queue()
            time.sleep(0.001)

    def run(self):
        print("\nRunning... Press Ctrl+C to exit")
        print("\nListening for:")
        print("1. CC messages 1-10 (PWM control)")
        print("2. MIDI notes (relay triggers)")
        
        # Start MIDI listener thread
        midi_thread = threading.Thread(target=self.midi_listener, daemon=True)
        midi_thread.start()
        
        # Start queue processor thread
        processor_thread = threading.Thread(target=self.queue_processor, daemon=True)
        processor_thread.start()
        
        try:
            while True:
                time.sleep(0.1)
        except KeyboardInterrupt:
            print("\nExiting...")
        except Exception as e:
            print(f"Error during operation: {e}")
        finally:
            self.close()

    def close(self):
        try:
            # Set all motors to zero
            for motor in range(1, 11):
                command = f"m{motor} 0\n"
                with self.serial_lock:
                    self.serial.write(command.encode())
                time.sleep(0.1)
            
            # Turn off all relays with mask of 0
            self.send_relay_command(0)
            
            if hasattr(self, 'midi_in'):
                self.midi_in.close()
            if hasattr(self, 'serial'):
                self.serial.close()
            print("Cleanup completed")
        except Exception as e:
            print(f"Error during cleanup: {e}")

if __name__ == "__main__":
    try:
        controller = MIDIController()
        controller.run()
    except Exception as e:
        print(f"Error: {e}")
def calculate_checksum(sentence):
    # Remove the starting '$' and the ending '*'
    sentence = sentence.strip('$').split('*')[0]
    
    # Initialize checksum to 0
    checksum = 0
    
    # XOR each character's ASCII value
    for char in sentence:
        checksum ^= ord(char)
    
    # Return the checksum as a two-digit hexadecimal string
    return f"{checksum:02X}"

def generate_nmea_sentence(params):
    # Construct the sentence without the checksum
    sentence = f"$PCAS03,{','.join(params)}"
    
    # Calculate the checksum
    checksum = calculate_checksum(sentence)
    
    # Append the checksum to the sentence
    full_sentence = f"{sentence}*{checksum}\r\n"
    
    return full_sentence

# Example parameters
params = ['5', '5', '5', '5', '0', '0', '5', '1', '0', '0', '', '', '1', '1', '', '', '', '1']

# Generate the full NMEA sentence with checksum
nmea_sentence = generate_nmea_sentence(params)
print(nmea_sentence)

import re
from collections import defaultdict

def parse_coordinates(line):
    """
    Parses a line containing coordinates and returns a tuple of floats (X, Y, Z).
    """
    match = re.search(r'Coordinates:\s*X\s*=\s*([-\d.]+),\s*Y\s*=\s*([-\d.]+),\s*Z\s*=\s*([-\d.]+)', line)
    if match:
        x, y, z = match.groups()
        return float(x), float(y), float(z)
    return None

first_line = ""
def process_file(file_path):
    global first_line
    """
    Processes the input file and consolidates entries with the same Coordinates address.
    Returns a nested dictionary structured by Base Address and Coordinates address.
    """
    data = defaultdict(lambda: defaultdict(lambda: {'coordinates': None, 'pointers': []}))
    current_base = None
    current_coord_addr = None  # Initialize to keep track of current Coordinates address
    with open(file_path, 'r') as file:
        first_line = file.readline()
        for line in file:
            line = line.strip()

            # Check for Base Address
            base_match = re.match(r'Base Address:\s*(0x[0-9a-fA-F]+)', line)
            if base_match:
                current_base = base_match.group(1)
                current_coord_addr = None  # Reset Coordinates address when a new Base Address is found
                continue

            if not current_base:
                # Skip lines until a Base Address is found
                continue

            # Check for Pointer address and Coordinates address
            pointer_match = re.match(r'Pointer address:\s*(0x[0-9a-fA-F]+)\s*/\s*Coordinates address:\s*(0x[0-9a-fA-F]+)', line)
            if pointer_match:
                pointer_addr, coord_addr = pointer_match.groups()
                current_pointer = pointer_addr
                current_coord_addr = coord_addr
                # Initialize in data structure
                if data[current_base][current_coord_addr]['coordinates'] is None:
                    data[current_base][current_coord_addr]['coordinates'] = None  # To be filled later
                data[current_base][current_coord_addr]['pointers'].append(pointer_addr)
                continue

            # Check for Coordinates line
            coord_values = parse_coordinates(line)
            if coord_values and current_base and current_coord_addr:
                data[current_base][current_coord_addr]['coordinates'] = coord_values
                continue

            # Optional: Handle other lines like RIP if necessary
            # For now, we skip them
            continue

    return data

def write_consolidated_data(data, output_file):
    """
    Writes the consolidated data to the specified output file in a readable format.
    """
    with open(output_file, 'w+') as f:
        f.write(first_line)
        for base_addr, coords_dict in data.items():
            f.write(f"Base Address: {base_addr}\n")
            for coord_addr, info in coords_dict.items():
                x, y, z = info['coordinates']
                f.write(f"    Coordinates address: {coord_addr}\n")
                f.write(f"    Coordinates: X = {x}, Y = {y}, Z = {z}\n")
                f.write(f"    Pointer addresses:\n")
                for ptr in info['pointers']:
                    f.write(f"        {ptr}\n")
                f.write("    ----------------------------------------\n")
            f.write("\n")  # Add an extra newline for better separation between Base Addresses

def main():
    input_file = 'float_pointers.txt'    # Replace with your input file path
    output_file = 'float_pointers-united.txt'  # Replace with your desired output file path

    data = process_file(input_file)
    write_consolidated_data(data, output_file)
    print(f"Consolidated data has been written to '{output_file}'.")

if __name__ == "__main__":
    main()

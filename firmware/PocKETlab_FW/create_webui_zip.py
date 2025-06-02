#!/usr/bin/env python3
"""
PocKETlab Web UI ZIP Creator
Creates an uncompressed ZIP file containing web interface files for upload to the device.
"""

import os
import zipfile
import sys
from pathlib import Path

def create_webui_zip(webui_dir="webui", output_file="webui.zip"):
    """
    Create an uncompressed ZIP file containing all files from the webui directory.
    
    Args:
        webui_dir (str): Directory containing web UI files
        output_file (str): Output ZIP file name
    """
    
    # Check if webui directory exists
    if not os.path.exists(webui_dir):
        print(f"Error: Web UI directory '{webui_dir}' not found!")
        print("Please create a 'webui' directory with your web interface files:")
        print("  - index.html")
        print("  - style.css") 
        print("  - app.js")
        print("  - (any other web files)")
        return False
    
    # Required files for a complete web UI
    required_files = ['index.html']
    recommended_files = ['style.css', 'app.js']
    
    # Check for required files
    missing_required = []
    missing_recommended = []
    
    for file in required_files:
        if not os.path.exists(os.path.join(webui_dir, file)):
            missing_required.append(file)
    
    for file in recommended_files:
        if not os.path.exists(os.path.join(webui_dir, file)):
            missing_recommended.append(file)
    
    if missing_required:
        print(f"Error: Missing required files: {', '.join(missing_required)}")
        return False
    
    if missing_recommended:
        print(f"Warning: Missing recommended files: {', '.join(missing_recommended)}")
    
    # Create the ZIP file with no compression
    try:
        with zipfile.ZipFile(output_file, 'w', zipfile.ZIP_STORED) as zipf:
            # Walk through all files in the webui directory
            for root, dirs, files in os.walk(webui_dir):
                for file in files:
                    file_path = os.path.join(root, file)
                    # Calculate the archive name (relative path from webui_dir)
                    archive_name = os.path.relpath(file_path, webui_dir)
                    # Convert Windows paths to forward slashes for web compatibility
                    archive_name = archive_name.replace('\\', '/')
                    
                    print(f"Adding: {archive_name}")
                    zipf.write(file_path, archive_name)
        
        # Get file size
        file_size = os.path.getsize(output_file)
        print(f"\nZIP file created successfully: {output_file}")
        print(f"File size: {file_size} bytes ({file_size/1024:.1f} KB)")
        print(f"Compression: NONE (ZIP_STORED method)")
        
        # List contents for verification
        print(f"\nZIP contents:")
        with zipfile.ZipFile(output_file, 'r') as zipf:
            for info in zipf.infolist():
                print(f"  {info.filename} ({info.file_size} bytes)")
        
        return True
        
    except Exception as e:
        print(f"Error creating ZIP file: {e}")
        return False

def main():
    """Main function to handle command line arguments and create the ZIP file."""
    
    print("PocKETlab Web UI ZIP Creator")
    print("=" * 40)
    
    # Parse command line arguments
    webui_dir = "webui"
    output_file = "webui.zip"
    
    if len(sys.argv) > 1:
        webui_dir = sys.argv[1]
    if len(sys.argv) > 2:
        output_file = sys.argv[2]
    
    print(f"Source directory: {webui_dir}")
    print(f"Output file: {output_file}")
    print()
    
    # Create the ZIP file
    success = create_webui_zip(webui_dir, output_file)
    
    if success:
        print("\n✓ Success! Upload the ZIP file using the device's web interface.")
        print("  The ZIP file uses no compression (ZIP_STORED) as required by the firmware.")
    else:
        print("\n✗ Failed to create ZIP file.")
        sys.exit(1)

if __name__ == "__main__":
    main()
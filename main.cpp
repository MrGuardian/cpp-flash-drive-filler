#define NOMINMAX // Prevents Windows.h from defining min and max macros
#include <windows.h>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <locale>
#include <filesystem> // C++17 filesystem library
#include <thread>
#include <atomic>
#include <mutex>
#include <condition_variable>

namespace fs = std::filesystem;

// Function to get the drive type from the drive letter
DWORD GetDriveTypeFromLetter(char driveLetter) {
    WCHAR drivePath[] = L"A:\\";
    drivePath[0] = driveLetter;
    return GetDriveTypeW(drivePath);
}

// Function to check if the drive is a USB device
bool IsUsbDevice(char driveLetter) {
    std::wstring volumeAccessPath = L"\\\\.\\X:";
    volumeAccessPath[4] = static_cast<wchar_t>(driveLetter);

    HANDLE deviceHandle = CreateFileW(
        volumeAccessPath.c_str(),
        0,                // no access to the drive
        FILE_SHARE_READ | // share mode
        FILE_SHARE_WRITE,
        NULL,             // default security attributes
        OPEN_EXISTING,    // disposition
        0,                // file attributes
        NULL);            // do not copy file attributes

    if (deviceHandle == INVALID_HANDLE_VALUE) {
        std::wcerr << L"Failed to open handle for drive: " << driveLetter << std::endl;
        return false;
    }

    // Setup query
    STORAGE_PROPERTY_QUERY query = {};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    // Allocate enough memory for the device descriptor
    BYTE buffer[1024];
    memset(buffer, 0, sizeof(buffer));

    // Issue query
    DWORD bytesReturned = 0;
    STORAGE_DEVICE_DESCRIPTOR* devd = reinterpret_cast<STORAGE_DEVICE_DESCRIPTOR*>(buffer);
    STORAGE_BUS_TYPE busType = BusTypeUnknown;

    if (DeviceIoControl(deviceHandle,
        IOCTL_STORAGE_QUERY_PROPERTY,
        &query, sizeof(query),
        devd, sizeof(buffer),
        &bytesReturned, NULL))
    {
        busType = devd->BusType;
    }
    else
    {
        std::wcerr << L"Failed to define bus type for: " << driveLetter << std::endl;
        CloseHandle(deviceHandle);
        return false;
    }

    CloseHandle(deviceHandle);

    return BusTypeUsb == busType;
}

// Helper function to format numbers with commas
std::string format_int_with_commas(ULONGLONG bytes) {
    std::ostringstream ss;
    ss.imbue(std::locale(""));
    ss << bytes;
    return ss.str();
}

// Function to update the progress bar
void update_progress_bar(std::atomic<ULONGLONG>& bytesWritten, ULONGLONG totalBytes, std::chrono::steady_clock::time_point start, std::atomic<bool>& writing_done) {
    static constexpr int barWidth = 50;

    while (!writing_done.load()) {
        ULONGLONG currentBytes = bytesWritten.load();

        auto now = std::chrono::steady_clock::now();
        double elapsedSeconds = std::chrono::duration<double>(now - start).count();
        double speed = elapsedSeconds > 0 ? static_cast<double>(currentBytes) / elapsedSeconds / (1024.0 * 1024.0) : 0.0;

        float progress = static_cast<float>(currentBytes) / totalBytes;
        int pos = static_cast<int>(barWidth * progress);
        int minutesRemaining = progress > 0 ? static_cast<int>((elapsedSeconds / progress) / 60.0) : 0;

        std::cout << "\r[" << std::string(pos, '=') << std::string(barWidth - pos, ' ') << "] "
                  << std::fixed << std::setprecision(2) << (progress * 100) << "% "
                  << format_int_with_commas(currentBytes) << "/" << format_int_with_commas(totalBytes) << " bytes "
                  << std::fixed << std::setprecision(2) << speed << " MB/s "
                  << "Remaining: " << (minutesRemaining > 0 ? std::to_string(minutesRemaining) : "Calculating") << " minutes. "
                  << std::flush;

        std::this_thread::sleep_for(std::chrono::milliseconds(500)); // Update every 500ms
    }

    // Final update after writing is done
    ULONGLONG finalBytes = bytesWritten.load();
    auto now = std::chrono::steady_clock::now();
    double elapsedSeconds = std::chrono::duration<double>(now - start).count();
    double speed = elapsedSeconds > 0 ? static_cast<double>(finalBytes) / elapsedSeconds / (1024.0 * 1024.0) : 0.0;
    float progress = static_cast<float>(finalBytes) / totalBytes;
    int pos = static_cast<int>(barWidth * progress);

    std::cout << "\r[" << std::string(pos, '=') << std::string(barWidth - pos, ' ') << "] "
              << std::fixed << std::setprecision(2) << (progress * 100) << "% "
              << format_int_with_commas(finalBytes) << "/" << format_int_with_commas(totalBytes) << " bytes "
              << std::fixed << std::setprecision(2) << speed << " MB/s "
              << "Remaining: 0 minutes. "
              << std::flush;
}

int main() {
    char drive_letter = 'E';
    std::cout << "Enter the drive letter of the USB device: ";
    std::cin >> drive_letter;

    drive_letter = std::toupper(static_cast<unsigned char>(drive_letter));

    std::cout << "WARNING: This program will irreversibly fill the USB drive " << drive_letter
              << ":\\ with random data." << std::endl;
    std::cout << "Ensure that you have backed up any important data before proceeding." << std::endl;
    char confirmation;
    std::cout << "Do you want to proceed? (y/n): ";
    std::cin >> confirmation;
    if (confirmation != 'y' && confirmation != 'Y') {
        std::cout << "Operation canceled by user." << std::endl;
        return 0;
    }

    bool is_drive_removable = GetDriveTypeFromLetter(drive_letter) == DRIVE_REMOVABLE;
    bool is_drive_usb = IsUsbDevice(drive_letter);

    // Validate drive letter
    if ((is_drive_removable && is_drive_usb) == false) {
        std::cerr << "Drive " << drive_letter << ": is not a removable USB drive." << std::endl;
        return 1;
    }

    constexpr size_t buffer_size = 1024 * 1024; // 1MB buffer
    std::vector<char> buffer(buffer_size);

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(0, 255);

    ULARGE_INTEGER free_bytes_available, totalNumberOfBytes, totalNumberOfFreeBytes;
    std::string drive_path = std::string(1, drive_letter) + ":\\";
    if (!GetDiskFreeSpaceExA(drive_path.c_str(),
                             &free_bytes_available,
                             &totalNumberOfBytes,
                             &totalNumberOfFreeBytes)) {
        std::cerr << "Failed to get disk space information." << std::endl;
        return 1;
    }

    ULONGLONG total_bytes_to_write = free_bytes_available.QuadPart;

    std::atomic<ULONGLONG> total_bytes_written(0); // Track total bytes written
    std::atomic<ULONGLONG> per_file_bytes_written(0); // Track bytes written per file
    const ULONGLONG max_file_size = static_cast<ULONGLONG>(4) * 1024 * 1024 * 1024; // 4GB
    int file_index = 0;

    std::atomic<bool> writing_done(false);
    auto start = std::chrono::steady_clock::now();

    // Start the progress bar thread
    std::thread progress_thread(update_progress_bar, std::ref(total_bytes_written), total_bytes_to_write, start, std::ref(writing_done));

    std::ofstream file;
    std::string filename;

    while (total_bytes_written < total_bytes_to_write) {
        if (per_file_bytes_written == 0) {
            // Find the next available file name
            while (true) {
                filename = drive_path + "filldata_" + std::to_string(file_index) + ".bin";
                if (fs::exists(filename)) {
                    // Skip existing file
                    std::cout << "\nSkipping existing file: " << filename << std::endl;
                    file_index++;
                } else {
                    break; // Found an unused file name
                }
            }

            // Open the new file
            file.open(filename, std::ios::binary | std::ios::out | std::ios::trunc);
            if (!file.is_open()) {
                std::cerr << "Failed to open file for writing: " << filename << std::endl;
                break;
            }
            file_index++;
        }

        // Determine how much to write in this iteration
        ULONGLONG bytes_remaining = total_bytes_to_write - total_bytes_written.load();
        ULONGLONG space_in_file = max_file_size - per_file_bytes_written.load();
        size_t write_size = static_cast<size_t>(std::min(static_cast<ULONGLONG>(buffer_size), std::min(bytes_remaining, space_in_file)));

        if (write_size == 0) {
            // No more space to write
            break;
        }

        // Fill buffer with random data
        for (size_t i = 0; i < write_size; ++i) {
            buffer[i] = static_cast<char>(distrib(gen));
        }

        // Write buffer to file
        file.write(buffer.data(), write_size);
        if (!file) {
            std::cerr << "\nFailed to write to file: " << filename << std::endl;
            file.close();
            break;
        }

        total_bytes_written += write_size;
        per_file_bytes_written += write_size;

        // Check if we've reached the maximum file size
        if (per_file_bytes_written >= max_file_size) {
            file.close();
            per_file_bytes_written = 0;
        }
    }

    // Close the file if it's still open
    if (file.is_open()) {
        file.close();
    }

    // Indicate that writing is done
    writing_done = true;

    // Wait for the progress thread to finish
    if (progress_thread.joinable()) {
        progress_thread.join();
    }

    std::cout << std::endl; // Ensure the last progress bar update is on a new line
    std::cout << "Total bytes written: " << format_int_with_commas(total_bytes_written.load()) << std::endl;
    return 0;
}

#include "stdafx.h"
#pragma warning (disable: 5043)
#include "../../../Common/ComTry.h"
#include "../../../Common/MyString.h"
#include "../../../Windows/PropVariant.h"
#include "../../Common/ProgressUtils.h"
#include "../../Common/RegisterArc.h"
#include "../../Common/StreamObjects.h"
#include "../../Archive/IArchive.h"
#include "../../Common/LimitedStreams.h"
#include "../../Common/StreamUtils.h"
#include "Pak.h"


using namespace NWindows;
namespace NArchive {
    namespace NZzz {

        // Define properties used by the handler
        static const Byte kProps[] =
        {
            kpidPath,  // Path property identifier
            kpidSize,  // Size property identifier
        };
          // CHandler class implements IInArchive and IInArchiveGetStream interfaces
        class CHandler : public IInArchive, public IInArchiveGetStream, public CMyUnknownImp {
        public:
            MY_UNKNOWN_IMP2(IInArchive, IInArchiveGetStream) // Implementing unknown interface
                INTERFACE_IInArchive(;)

                // Default constructor
                CHandler() = default;

            // Constructor that accepts a PyInstallerHandler reference
            CHandler(PakFileHandler& handler) : pakHandler(handler) {}

            // Retrieves a stream from the archive
            STDMETHOD(GetStream)(UInt32 index, ISequentialInStream** stream);

            PakFileHandler pakHandler; // Instance of PyInstallerHandler
            std::vector<PakFileHandler::Record> items; // Vector to store archive entries
        };

        // Implement interface functions for the archive properties
        IMP_IInArchive_Props
            IMP_IInArchive_ArcProps_NO_Table

            /**
                * @brief Retrieves a property of the archive based on the provided property ID.
                *
                * @param propID The ID of the property to retrieve.
                * @param value Output parameter to receive the property value.
                * @return HRESULT indicating success or failure.
                */
            STDMETHODIMP CHandler::GetArchiveProperty(PROPID propID, PROPVARIANT* value)
        {
            NCOM::CPropVariant prop; // Property variant to hold the property value
            switch (propID)
            {
            case kpidPhySize:
                prop = (UInt64)1; // Assign a physical size value
                break;
            }
            prop.Detach(value); // Detach the property from the variant
            return S_OK; // Indicate success
        }

        /**
        * @brief Opens the specified input stream as an archive.
        *
        * @param stream The input stream to open.
        * @param callback Callback interface for reporting progress and status.
        * @return HRESULT indicating success or failure.
        */
        STDMETHODIMP CHandler::Open(IInStream* stream, const UInt64*, IArchiveOpenCallback* callback) {
            Close(); // Close any existing archive state

            // Validate input parameters
            if (!callback || !stream) {
                return S_FALSE; // Invalid arguments
            }


            // Attempt to open the PyInstaller handler
            HRESULT result = pakHandler.Open(stream, nullptr, callback);
            if (FAILED(result)) {
                return result; // Return if the handler fails to open
            }

            items = pakHandler.items; // Retrieve items from the handler

            // Debug message to check the number of items after opening
            std::wstring msg = L"Size of items vector after PyInstallerHandler::Open: " + std::to_wstring(items.size());

            MessageBox(NULL, msg.c_str(), L"Debug - CHandler::Open", MB_OK);

            UInt64 fileSize = 0; // Declare file size variable
            result = stream->Seek(0, STREAM_SEEK_END, &fileSize); // Seek to the end to get the file size
            if (FAILED(result) || fileSize == 0) {
                return S_FALSE; // Ensure the file size is valid
            }
            stream->Seek(0, STREAM_SEEK_SET, nullptr); // Seek back to the start of the stream
            // Get the name of the file from the callback
            CMyComPtr<IArchiveOpenVolumeCallback> volumeCallback;
            result = callback->QueryInterface(IID_IArchiveOpenVolumeCallback, (void**)&volumeCallback);
            if (FAILED(result) || !volumeCallback) {
                return S_FALSE; // Check that the interface was obtained
            }
            UString name;
            {
                NCOM::CPropVariant prop;
                result = volumeCallback->GetProperty(kpidName, &prop);
                if (FAILED(result) || prop.vt != VT_BSTR) {
                    return S_FALSE; // Ensure the property is a string
                }
                name = prop.bstrVal; // Get the name as UString
            }

        }

        /**
        * @brief Closes the archive and releases resources.
        *
        * @return HRESULT indicating success.
        */
        STDMETHODIMP CHandler::Close() {
            return S_OK; // Indicate successful closure
        }

        /**
        * @brief Retrieves the number of items in the archive.
        *
        * @param numItems Output parameter to receive the number of items.
        * @return HRESULT indicating success or failure.
        */
        STDMETHODIMP CHandler::GetNumberOfItems(UInt32* numItems) {
            if (numItems == nullptr) {
                return E_POINTER; // Return error if pointer is null
            }

            // Access the number of items through the PyInstallerHandler instance
            *numItems = static_cast<UInt32>(pakHandler.items.size()); // Correctly access size of vector

            // Debug message to show the number of items
            std::wstring msg = L"Total items: " + std::to_wstring(*numItems);

            MessageBox(NULL, msg.c_str(), L"Debug - GetNumberOfItems", MB_OK);

            return S_OK; // Indicate success
        }

        /**
        * @brief Retrieves a specific property for an item in the archive.
        *
        * @param index The index of the item.
        * @param propID The property ID to retrieve.
        * @param value Output parameter to receive the property value.
        * @return HRESULT indicating success or failure.
        */
        STDMETHODIMP CHandler::GetProperty(UInt32 index, PROPID propID, PROPVARIANT* value)
        {
            if (index >= items.size()) {
                return E_INVALIDARG; // Handle out-of-bounds index
            }

            NCOM::CPropVariant prop; // Property variant to hold the value
            switch (propID)
            {
            case kpidPath:
                if (!items[index].fileName.empty()) {
                    prop = items[index].fileName.c_str(); // Set the path property
                }
                else {
                    return E_INVALIDARG; // Return error for empty file name
                }
                break;

            case kpidSize:
                prop = items[index].sizeDecompressed; // Set the decompressed size property
                break;

            default:
                return E_INVALIDARG; // Return error if propID is not recognized
            }

            prop.Detach(value); // Detach the property from the variant
            return S_OK; // Indicate success
        }



        /**
        * @brief Extracts items from the archive.
        *
        * @param indices Array of indices for items to extract.
        * @param numItems Number of items to extract.
        * @param testMode Flag indicating if this is a test extraction.
        * @param extractCallback Callback for extraction progress and results.
        * @return HRESULT indicating success or failure.
        */
        STDMETHODIMP CHandler::Extract(const UInt32* indices, UInt32 numItems, Int32 testMode, IArchiveExtractCallback* extractCallback) {
            bool allFilesMode = (numItems == (UInt32)(Int32)-1);
            if (allFilesMode) numItems = static_cast<UInt32>(items.size());
            if (numItems == 0) return S_OK;

            UInt64 totalSize = 0, currentSize = 0;
            for (size_t i = 0; i < numItems; i++) {
                totalSize += items[allFilesMode ? i : indices[i]].sizeDecompressed;
            }
            extractCallback->SetTotal(totalSize);

            CLocalProgress* lps = new CLocalProgress;
            CMyComPtr<ICompressProgressInfo> progress = lps;
            lps->Init(extractCallback, false);

            for (UINT i = 0; i < numItems; i++) {
                lps->InSize = currentSize;
                lps->OutSize = currentSize;
                RINOK(lps->SetCur());

                CMyComPtr<ISequentialOutStream> realOutStream;
                Int32 askMode = testMode ? NExtract::NAskMode::kTest : NExtract::NAskMode::kExtract;
                UINT32 index = allFilesMode ? i : indices[i];
                auto& item = items[index];
                currentSize += item.sizeDecompressed;

                RINOK(extractCallback->GetStream(index, &realOutStream, askMode));
                if (!testMode && !realOutStream) {
                    MessageBox(NULL, L"[Debug] Skipping file due to missing output stream.", L"Debug Info", MB_OK);
                    continue;
                }

                RINOK(extractCallback->PrepareOperation(askMode));

                std::wstringstream debugMsg;
                debugMsg << L"[Debug] Extracting file index " << index << L":\n"
                    << L"  Name: " << item.fileName.c_str();
                   
                MessageBox(NULL, debugMsg.str().c_str(), L"Debug Info", MB_OK);

                // Check data content
                std::wstringstream contentMsg;
                contentMsg << L"[Debug] Data content for file index " << index << L": ";
                for (const auto& byte : item.Data) {
                    contentMsg << byte;
                }
                MessageBox(NULL, contentMsg.str().c_str(), L"Debug Info", MB_OK);

                HRESULT writeResult = realOutStream->Write(item.Data.data(), (UINT32)item.Data.size(), NULL);
                if (writeResult != S_OK) {
                    std::wstringstream errMsg;
                    errMsg << L"[Debug] Failed to write data for file index " << index << L". Data size: " << item.Data.size();
                    MessageBox(NULL, errMsg.str().c_str(), L"Debug Info", MB_OK);
                    return writeResult;
                }

                std::wstringstream successMsg;
                successMsg << L"[Debug] Successfully wrote data for file index " << index << L". Data size: " << item.Data.size();
                MessageBox(NULL, successMsg.str().c_str(), L"Debug Info", MB_OK);

                realOutStream.Release();
                RINOK(extractCallback->SetOperationResult(NExtract::NOperationResult::kOK));
            }

            lps->InSize = totalSize;
            lps->OutSize = totalSize;
            return lps->SetCur();
        }


        /**
        * @brief Retrieves a stream corresponding to a specific item in the archive.
        *
        * @param index The index of the item for which to retrieve the stream.
        * @param stream Output parameter to receive the stream.
        * @return HRESULT indicating success or failure.
        */
        STDMETHODIMP CHandler::GetStream(UInt32 index, ISequentialInStream** stream)
        {
            if (index >= items.size()) {
                return E_FAIL; // Safety check for index bounds
            }

            *stream = nullptr; // Initialize the output stream pointer to nullptr

            // Ensure the item has valid data to create a stream
            if (items[index].Data.empty()) {
                return E_FAIL; // Return error if the data is empty
            }

            // Create a buffer stream for the item data
            CBufInStream* streamSpec = new CBufInStream;
            CMyComPtr<ISequentialInStream> streamTemp = streamSpec; // Smart pointer management

            // Initialize the stream with the item data
            streamSpec->Init(reinterpret_cast<const Byte*>(items[index].Data.data()), static_cast<UInt32>(items[index].Data.size()));

            // If Init doesn't throw or return errors, assign the stream to the output parameter
            *stream = streamTemp.Detach();
            return S_OK; // Indicate success
        }





        // Register the archive handler for .exe files
        REGISTER_ARC_I_NO_SIG(
            "pak", "pak", 0, 0xAA,
            0,
            0,
            NULL)
    }
}
#include <filesystem>
#include <memory>
//
#include "expp/app/explorer.hpp"    
#include "expp/app/explorer_view.hpp"

int main(int argc, char* argv[])
{
    using namespace expp;

    std::filesystem::path start_path = std::filesystem::current_path();

    if (argc > 1)
    {
        std::filesystem::path arg_path = argv[1];
        if (std::filesystem::exists(arg_path) && std::filesystem::is_directory(arg_path))
        {
            start_path = std::filesystem::canonical(arg_path);  
        }
    }
    //// Create explorer controller
    [[maybe_unused]] auto explorer = std::make_shared<app::Explorer>(start_path);

    app::ExplorerView view(explorer);
    view.run();
}

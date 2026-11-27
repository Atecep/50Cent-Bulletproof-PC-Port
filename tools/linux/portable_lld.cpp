// Build the portable link driver with the ELF backend only.
#include <lld/Common/Driver.h>
#include <llvm/Support/InitLLVM.h>

LLD_HAS_DRIVER(elf)

int main(int argc, char** argv) {
    llvm::InitLLVM init(argc, argv);
    constexpr lld::DriverDef drivers[]{{lld::Gnu, lld::elf::link}};
    return lld::lldMain(llvm::ArrayRef<const char*>(argv, argc), llvm::outs(), llvm::errs(), drivers).retCode;
}

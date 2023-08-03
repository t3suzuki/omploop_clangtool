// Declares clang::SyntaxOnlyAction.
#include "clang/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
// Declares llvm::cl::extrahelp.
#include "llvm/Support/CommandLine.h"

#include "clang/Driver/Options.h"
#include "clang/AST/AST.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/ASTConsumer.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Rewrite/Core/Rewriter.h"

#include <stdio.h>

using namespace std;
using namespace clang;
using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;


class MyVisitor : public RecursiveASTVisitor<MyVisitor> {
private:
  ASTContext *astContext; // used for getting additional AST info
  Rewriter *rewriter;
public:
  explicit MyVisitor(CompilerInstance *CI, Rewriter *R) 
      : astContext(&(CI->getASTContext())) // initialize private members
    {
      rewriter = R;
    }
    virtual bool VisitStmt(Stmt *st) {
      //st->dump();
      return true;
    }
    virtual bool VisitOMPParallelForDirective(OMPParallelForDirective *omp) {
      /*
      omp->dump();
      omp->dumpPretty(*astContext);
      const Stmt *BodyStmt = omp->getBody();
      omp->getCond()->dumpPretty(*astContext);
      omp->getPreCond()->dumpPretty(*astContext);
      omp->getInit()->dumpPretty(*astContext);
      omp->getInc()->dumpPretty(*astContext);
      printf("\n1...\n");
      omp->getLastIteration()->dumpPretty(*astContext);
      printf("\n1...\n");
      omp->getUpperBoundVariable()->dumpPretty(*astContext);
      printf("\n1...\n");
      omp->getNextUpperBound()->dumpPretty(*astContext);
      printf("\n1...\n");
      omp->getEnsureUpperBound()->dumpPretty(*astContext);
      printf("\n1...\n");
      for (const Expr *u: omp->updates()) {
	u->dumpPretty(*astContext);
      }
      printf("\n2...\n");
      for (const Expr *ini: omp->inits()) {
	ini->dumpPretty(*astContext);
      }
      printf("\n3...\n");
      for (const Expr *fc: omp->finals_conditions()) {
	fc->dumpPretty(*astContext);
      }
      printf("\n4...\n");
      for (const Expr *f: omp->finals()) {
	f->dumpPretty(*astContext);
      }
      */

      std::string str_init;
      llvm::raw_string_ostream os_init(str_init);
      for (const Expr *ini: omp->inits()) {
	ini->printPretty(os_init, nullptr, PrintingPolicy(astContext->getLangOpts()));
      }

      std::string str_cond;
      llvm::raw_string_ostream os_cond(str_cond);
      omp->getCond()->printPretty(os_cond, nullptr, PrintingPolicy(astContext->getLangOpts()));
      {
	std::size_t pos1 = str_cond.find(".omp.iv");
	str_cond.replace(pos1, 7, "my_th_iter");
	std::size_t pos2 = str_cond.find(".omp.ub");
	str_cond.replace(pos2, 7, "my_th_iter_max");
      }
      
      std::string str_update;
      llvm::raw_string_ostream os_update(str_update);
      for (const Expr *u: omp->updates()) {
	//u->dump();
	u->printPretty(os_update, nullptr, PrintingPolicy(astContext->getLangOpts()));
      }
      std::size_t pos = str_update.find(".omp.iv");
      str_update.replace(pos, 7, "my_th_iter");
      
      std::string str_head;
      llvm::raw_string_ostream os_head(str_head);

      os_head << "\n auto "
	      << str_init
	      << ";\n"
	"  int my_th_iter = 0;\n"
	"  int my_th_iter_max = ";
      omp->getLastIteration()->printPretty(os_head, nullptr, PrintingPolicy(astContext->getLangOpts()));

      std::string str_body;
      llvm::raw_string_ostream os_body(str_body);
      omp->getBody()->printPretty(os_body, nullptr, PrintingPolicy(astContext->getLangOpts()));
      os_head << ";\n"
	"  int n_done = 0;\n"
	"  int stat[N_CORO];\n"
	"  bzero(stat, N_CORO*sizeof(int));\n"
	"  do {\n"
	"    for (int i_coro=0; i_coro<N_CORO; i_coro++) {\n"
	"      switch (stat[i_coro]) {\n"
	"        case -1: goto __exit;\n"
	"        case 0:\n"
	"          while (1) {\n"
	"            if (!("
	      << str_cond
	      <<
	")) {\n"
	"              stat[i_coro] = -1;\n"
	"              n_done ++;\n"
	"              goto __exit;\n"
	"            } // if\n"
	      << str_update
	      << ";\n"
	"            my_th_iter++;\n"
	"            stat[i_coro] = __LINE__; goto __exit; case __LINE__: \n"
	      << str_body
	      << "\n"
	"          } // while (1)\n"
	"      } // switch (stat[i_coro])\n"
	"    __exit:\n"
	"    } // for i_coro\n"
	"  } while (n_done < N_CORO);\n";
      rewriter->RemoveText(omp->getSourceRange());
      rewriter->ReplaceText(omp->getInnermostCapturedStmt()->getSourceRange(), str_head);
      return true;
    }
};

class MyASTConsumer : public ASTConsumer {
private:
  MyVisitor *visitor;
public:
  MyASTConsumer(CompilerInstance *CI, Rewriter *R) {
    visitor = new MyVisitor(CI, R);
  }
  virtual void HandleTranslationUnit(ASTContext &Context) override {
    visitor->TraverseDecl(Context.getTranslationUnitDecl());
  }
};

class MyAction : public ASTFrontendAction {
public:
  // Output the edit buffer for this translation unit
  void EndSourceFileAction() override {
    MyRewriter.getEditBuffer(MyRewriter.getSourceMgr().getMainFileID())
      .write(llvm::outs());
  }
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &CI,
						 StringRef file) override {
    MyRewriter.setSourceMgr(CI.getSourceManager(), CI.getLangOpts());
    return std::make_unique<MyASTConsumer>(&CI, &MyRewriter);
  }
private:
  Rewriter MyRewriter;
};
  
// Apply a custom category to all command-line options so that they are the
// only ones displayed.
static llvm::cl::OptionCategory MyToolCategory("my-tool options");

// CommonOptionsParser declares HelpMessage with a description of the common
// command-line options related to the compilation database and input files.
// It's nice to have this help message in all tools.
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);

// A help message for this specific tool can be added afterwards.
static cl::extrahelp MoreHelp("\nMore help text...\n");

int main(int argc, const char **argv) {
  auto ExpectedParser = CommonOptionsParser::create(argc, argv, MyToolCategory);
  if (!ExpectedParser) {
    // Fail gracefully for unsupported options.
    llvm::errs() << ExpectedParser.takeError();
    return 1;
  }
  CommonOptionsParser& OptionsParser = ExpectedParser.get();
  ClangTool Tool(OptionsParser.getCompilations(),
                 OptionsParser.getSourcePathList());
  return Tool.run(newFrontendActionFactory<MyAction>().get());
}

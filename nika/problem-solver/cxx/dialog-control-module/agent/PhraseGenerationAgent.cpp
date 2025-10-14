#include "PhraseGenerationAgent.hpp"

#include "keynodes/DialogKeynodes.hpp"
#include "keynodes/MessageKeynodes.hpp"
#include "searcher/LanguageSearcher.hpp"
#include <common/handler/LinkHandler.hpp>
#include <common/utils/ScTemplateUtils.hpp>
#include <inference/template_manager.hpp>
#include <regex>
#include <sc-agents-common/utils/CommonUtils.hpp>
#include <sc-agents-common/utils/GenerationUtils.hpp>
#include <sc-agents-common/utils/IteratorUtils.hpp>

using namespace utils;

namespace dialogControlModule
{

PhraseGenerationAgent::PhraseGenerationAgent()
{
  m_logger = utils::ScLogger(
      utils::ScLogger::ScLogType::File, "logs/PhraseGenerationAgent.log", utils::ScLogLevel::Debug, true);
}

ScResult PhraseGenerationAgent::DoProgram(ScActionInitiatedEvent const & event, ScAction & action)
{
  ScAddr const & replyMessageNode = action.GetArgument(ScKeynodes::rrel_1);
  if (!m_context.IsElement(replyMessageNode))
  {
    m_logger.Error("Action doesn't have a reply message node.");
    return action.FinishWithError();
  }
  ScAddr const & phraseLink = action.GetArgument(ScKeynodes::rrel_2);
  if (!m_context.IsElement(phraseLink))
  {
    m_logger.Error("Action doesn't have a link with a text template.");
    return action.FinishWithError();
  }
  ScAddr templateNode =
      IteratorUtils::getAnyByOutRelation(&m_context, phraseLink, DialogKeynodes::nrel_phrase_template);
  ScAddr const & parametersNode = action.GetArgument(ScKeynodes::rrel_3);
  if (!m_context.IsElement(parametersNode))
  {
    m_logger.Error("Action doesn't have a parameters node.");
    return action.FinishWithError();
  }

  ScAddr const & linkResult = generateLinkByTemplate(templateNode, parametersNode, phraseLink);
  if (!m_context.IsElement(linkResult))
  {
    m_logger.Error("Answer isn't found.");
    return action.FinishUnsuccessfully();
  }
  LanguageSearcher searcher(&m_context, &m_logger);
  ScAddr const & langNode = searcher.getLanguage(phraseLink);
  if (m_context.IsElement(langNode))
  {
    m_context.GenerateConnector(ScType::ConstPermPosArc, langNode, linkResult);
  }
  else
  {
    m_logger.Debug("Language link isn't found.");
  }
  if (!m_context.IsElement(templateNode))
  {
    updateSemanticAnswer(phraseLink);
  }
  generateSemanticEquivalent(replyMessageNode, templateNode);

  action.SetResult(linkResult);

  return action.FinishSuccessfully();
}

ScAddr PhraseGenerationAgent::GetActionClass() const
{
  return MessageKeynodes::action_phrase_generation;
}

ScAddr PhraseGenerationAgent::generateLinkByTemplate(
    const ScAddr & templateNode,
    const ScAddr & parametersNode,
    const ScAddr & phraseLink)
{
  commonModule::LinkHandler handler(&m_context);
  std::string textResult;
  ScAddr linkResult;

  std::string text;
  m_context.GetLinkContent(phraseLink, text);
  std::map<VariableType, std::vector<std::string>> variables = getTemplateVariables(text);
  if (!variables.empty())
  {
    textResult = findResultText(templateNode, parametersNode, variables, text);
  }
  else
  {
    m_logger.Debug("Text template doesn't have variables.");
    textResult = text;
  }
  if (!textResult.empty())
  {
    linkResult = handler.createLink(textResult);
    std::string linkContent;
    m_context.GetLinkContent(linkResult, linkContent);
    m_logger.Debug("Generated text: \"", linkContent, "\"");
  }
  return linkResult;
}

std::map<VariableType, std::vector<std::string>> PhraseGenerationAgent::getTemplateVariables(std::string const & text)
{
  std::map<VariableType, std::vector<std::string>> variables;

  std::vector<std::string> linksIdentifiers = getVariableNames(text, PhraseGenerationAgent::LINK_VAR_REGULAR);
  if (!linksIdentifiers.empty())
  {
    variables.insert(std::make_pair(LINK, linksIdentifiers));
  }

  std::vector<std::string> setElementsIdentifiers =
      getVariableNames(text, PhraseGenerationAgent::SET_ELEMENTS_VAR_REGULAR);
  if (!setElementsIdentifiers.empty())
  {
    variables.insert(std::make_pair(SET_ELEMENTS, setElementsIdentifiers));
  }

  std::vector<std::string> setPowerIdentifiers = getVariableNames(text, PhraseGenerationAgent::SET_POWER_REGULAR);
  if (!setPowerIdentifiers.empty())
  {
    variables.insert(std::make_pair(SET_POWER, setPowerIdentifiers));
  }

  std::vector<std::string> classIdentifiers = getVariableNames(text, PhraseGenerationAgent::CLASS_REGULAR);
  if (!classIdentifiers.empty())
  {
    variables.insert(std::make_pair(CLASS_FIND, classIdentifiers));
  }

  std::vector<std::string> subareaIdentifiers = getVariableNames(text, PhraseGenerationAgent::SUBAREA_REGULAR);
  if (!subareaIdentifiers.empty())
  {
    variables.insert(std::make_pair(SUBAREA_FIND, subareaIdentifiers));
  }

  return variables;
}

std::string PhraseGenerationAgent::findResultText(
    const ScAddr & templateNode,
    const ScAddr & parametersNode,
    std::map<VariableType, std::vector<std::string>> const & variables,
    const std::string & text)
{
  std::string textResult;
  if (templateNode.IsValid())
  {
    std::vector<ScTemplateParams> parametersList = findParametersList(templateNode, parametersNode);
    for (auto & parameters : parametersList)
    {
      textResult = processScTemplate(templateNode, parameters, variables, text);
      if (!textResult.empty())
      {
        break;
      }
    }
  }
  else
  {
    m_logger.Debug("Phrase template isn't found.");
  }
  return textResult;
}

std::vector<ScTemplateParams> PhraseGenerationAgent::findParametersList(
    const ScAddr & templateNode,
    const ScAddr & parametersNode)
{
  inference::TemplateManager manager(&m_context);

  ScAddrVector arguments = IteratorUtils::getAllWithType(&m_context, parametersNode, ScType::Node);
  manager.SetArguments(arguments);
  std::vector<ScTemplateParams> parametersList;
  if (parametersNode.IsValid())
  {
    parametersList = manager.CreateTemplateParams(templateNode);
  }
  if (parametersList.empty())
  {
    m_logger.Debug("Didn't find any template parameters.");
    parametersList.emplace_back();
  }
  return parametersList;
}

std::vector<std::string> PhraseGenerationAgent::getVariableNames(
    std::string const & text,
    const std::string regular_str)
{
  std::vector<std::string> variables;
  std::regex regular(regular_str);
  std::smatch result;
  std::size_t regularElementsBeforeNameSize = regular_str.length() - 8;
  std::size_t regularElementsSize = regular_str.length() - 7;
  std::string::const_iterator searchStart(text.begin());
  while (regex_search(searchStart, text.cend(), result, regular))
  {
    std::string variable = result[0].str();
    variable = variable.substr(regularElementsBeforeNameSize, variable.length() - regularElementsSize);
    variables.push_back(variable);
    searchStart = result.suffix().first;
  }

  return variables;
}

std::string PhraseGenerationAgent::processScTemplate(
    ScAddr const & templateNode,
    ScTemplateParams const & parameters,
    std::map<VariableType, std::vector<std::string>> const & variables,
    std::string const & text)
{
  std::string textResult;

  ScTemplate templateOption;
  m_context.BuildTemplate(templateOption, templateNode, parameters);
  m_context.SearchByTemplateInterruptibly(
      templateOption,
      [this, &variables, &textResult, &text](ScTemplateSearchResultItem const & item) -> ScTemplateSearchRequest {
        textResult = generatePhraseAnswer(item, variables, text);
        updateSemanticAnswer(item);
        return ScTemplateSearchRequest::STOP;
      });

  return textResult;
}

void PhraseGenerationAgent::generateSemanticEquivalent(const ScAddr & replyMessageNode, const ScAddr & templateNode)
{
  ScAddr semanticEquivalent = m_context.GenerateNode(ScType::ConstNodeStructure);
  ScIterator3Ptr semanticEquivalentIterator =
      m_context.CreateIterator3(MessageKeynodes::answer_structure, ScType::ConstPermPosArc, ScType::Unknown);

  while (semanticEquivalentIterator->Next())
  {
    utils::GenerationUtils::addToSet(&m_context, semanticEquivalent, semanticEquivalentIterator->Get(2));
  }

  ScTemplate semanticEquivalentStructure;
  semanticEquivalentStructure.Quintuple(
      replyMessageNode,
      ScType::VarCommonArc,
      semanticEquivalent,
      ScType::VarPermPosArc,
      MessageKeynodes::nrel_semantic_equivalent);
  ScTemplateGenResult result;

  m_context.GenerateByTemplate(semanticEquivalentStructure, result);
}

std::string PhraseGenerationAgent::generatePhraseAnswer(
    ScTemplateSearchResultItem const & phraseSemanticResult,
    std::map<VariableType, std::vector<std::string>> const & variables,
    std::string const & text)
{
  std::string textResult = text;
  PhraseGenerationAgent::findNotSearchableElements();
  for (auto const & variable : variables)
  {
    switch (variable.first)
    {
    case LINK:
      replaceLinksVariables(phraseSemanticResult, variable.second, textResult);
      break;
    case SET_ELEMENTS:
      replaceSetElementsVariables(phraseSemanticResult, variable.second, textResult);
      break;
    case SET_POWER:
      replaceSetPowerVariables(phraseSemanticResult, variable.second, textResult);
      break;
    case CLASS_FIND:
      replaceClassVariables(phraseSemanticResult, variable.second, textResult);
      break;
    case SUBAREA_FIND:
      replaceSubareaVariables(phraseSemanticResult, variable.second, textResult);
      break;
    default:
      break;
    }
  }
  return textResult;
}

void PhraseGenerationAgent::replaceLinksVariables(
    ScTemplateSearchResultItem const & phraseSemanticResult,
    std::vector<std::string> const & variables,
    std::string & text)
{
  for (std::string const & variable : variables)
  {
    if (!phraseSemanticResult.Has(variable))
    {
      text = "";
      break;
    }
    ScAddr link = phraseSemanticResult[variable];
    std::string linkValue;
    m_context.GetLinkContent(link, linkValue);
    std::string variableRegular =
        regex_replace(PhraseGenerationAgent::VAR_REGULAR, std::regex(PhraseGenerationAgent::VAR_CONST), variable);
    text = regex_replace(text, std::regex(variableRegular), linkValue);
  }
}

void PhraseGenerationAgent::replaceSetElementsVariables(
    ScTemplateSearchResultItem const & phraseSemanticResult,
    std::vector<std::string> const & variables,
    std::string & text)
{
  std::stringstream setElementsTextStream;
  for (std::string const & variable : variables)
  {
    if (!phraseSemanticResult.Has(variable))
    {
      text = "";
      break;
    }
    ScAddr set = phraseSemanticResult[variable];
    ScIterator3Ptr const & setElementsIterator =
        m_context.CreateIterator3(set, ScType::ConstPermPosArc, ScType::ConstNode);
    if (setElementsIterator->Next())
    {
      setElementsTextStream << CommonUtils::getMainIdtf(&m_context, setElementsIterator->Get(2), {ScKeynodes::lang_ru});
      m_context.GenerateConnector(
          ScType::ConstPermPosArc, MessageKeynodes::answer_structure, setElementsIterator->Get(1));
      m_context.GenerateConnector(
          ScType::ConstPermPosArc, MessageKeynodes::answer_structure, setElementsIterator->Get(2));
    }
    while (setElementsIterator->Next())
    {
      setElementsTextStream << ", "
                            << CommonUtils::getMainIdtf(&m_context, setElementsIterator->Get(2), {ScKeynodes::lang_ru});
      m_context.GenerateConnector(
          ScType::ConstPermPosArc, MessageKeynodes::answer_structure, setElementsIterator->Get(1));
      m_context.GenerateConnector(
          ScType::ConstPermPosArc, MessageKeynodes::answer_structure, setElementsIterator->Get(2));
    }

    std::string variableRegular = regex_replace(
        PhraseGenerationAgent::SET_ELEMENTS_VAR_REGULAR, std::regex(PhraseGenerationAgent::VAR_CONST), variable);
    text = regex_replace(text, std::regex(variableRegular), setElementsTextStream.str());
  }
}

void PhraseGenerationAgent::replaceSetPowerVariables(
    ScTemplateSearchResultItem const & phraseSemanticResult,
    std::vector<std::string> const & variables,
    std::string & text)
{
  for (std::string const & variable : variables)
  {
    if (!phraseSemanticResult.Has(variable))
    {
      text = "";
      break;
    }
    ScAddr set = phraseSemanticResult[variable];
    std::string strSetPower;
    ScIterator5Ptr const & setIterator = m_context.CreateIterator5(
        set, ScType::EdgeDCommon, ScType::LinkConst, ScType::ConstPermPosArc, DialogKeynodes::nrel_set_power);

    if (!setIterator->Next())
    {
      size_t setPower = CommonUtils::getSetPower(&m_context, set);
      strSetPower = std::to_string(setPower);
      ScAddr const & setPowerAddr = m_context.GenerateLink(ScType::LinkConst);
      m_context.SetLinkContent(setPowerAddr, strSetPower);
      ScAddr const & arcCommonAddr = m_context.GenerateConnector(ScType::ConstCommonArc, set, setPowerAddr);
      ScAddr const & arcAccessAddr =
          m_context.GenerateConnector(ScType::ConstPermPosArc, DialogKeynodes::nrel_set_power, arcCommonAddr);
      m_context.GenerateConnector(ScType::ConstPermPosArc, MessageKeynodes::answer_structure, setPowerAddr);
      m_context.GenerateConnector(ScType::ConstPermPosArc, MessageKeynodes::answer_structure, arcCommonAddr);
      m_context.GenerateConnector(
          ScType::ConstPermPosArc, MessageKeynodes::answer_structure, DialogKeynodes::nrel_set_power);
      m_context.GenerateConnector(ScType::ConstPermPosArc, MessageKeynodes::answer_structure, arcAccessAddr);
    }
    else
      m_context.GetLinkContent(setIterator->Get(2), strSetPower);

    std::string variableRegular = regex_replace(
        PhraseGenerationAgent::SET_POWER_VAR_REGULAR, std::regex(PhraseGenerationAgent::VAR_CONST), variable);
    text = regex_replace(text, std::regex(variableRegular), strSetPower);
  }
}

void PhraseGenerationAgent::replaceClassVariables(
    ScTemplateSearchResultItem const & phraseSemanticResult,
    std::vector<std::string> const & variables,
    std::string & text)
{
  std::stringstream classElementsTextStream;
  for (std::string const & variable : variables)
  {
    if (!phraseSemanticResult.Has(variable))
    {
      text = "";
      break;
    }
    ScAddr entity = phraseSemanticResult[variable];
    ScIterator3Ptr const & classElementsIterator =
        m_context.CreateIterator3(ScType::NodeClass, ScType::ConstPermPosArc, entity);
    while (classElementsIterator->Next())
    {
      if (std::find(
              PhraseGenerationAgent::notSearchable.begin(),
              PhraseGenerationAgent::notSearchable.end(),
              classElementsIterator->Get(0)) == PhraseGenerationAgent::notSearchable.end())
      {
        classElementsTextStream << CommonUtils::getMainIdtf(
            &m_context, classElementsIterator->Get(0), {ScKeynodes::lang_ru});
        m_context.GenerateConnector(
            ScType::ConstPermPosArc, MessageKeynodes::answer_structure, classElementsIterator->Get(0));
        m_context.GenerateConnector(
            ScType::ConstPermPosArc, MessageKeynodes::answer_structure, classElementsIterator->Get(1));
        classElementsTextStream << ", ";
      }
    }
    std::string result = classElementsTextStream.str();
    if (result.size() > 2)
      result.erase(result.size() - 2);
    std::string variableRegular =
        regex_replace(PhraseGenerationAgent::CLASS_VAR_REGULAR, std::regex(PhraseGenerationAgent::VAR_CONST), variable);
    text = regex_replace(text, std::regex(variableRegular), result);
  }
}

void PhraseGenerationAgent::replaceSubareaVariables(
    ScTemplateSearchResultItem const & phraseSemanticResult,
    std::vector<std::string> const & variables,
    std::string & text)
{
  std::stringstream subareaElementsTextStream;
  for (std::string const & variable : variables)
  {
    if (!phraseSemanticResult.Has(variable))
    {
      text = "";
      break;
    }
    const ScAddr entity = phraseSemanticResult[variable];
    ScAddrVector maxStudiedNodes =
        IteratorUtils::getAllByInRelation(&m_context, entity, DialogKeynodes::rrel_maximum_studied_object_class);
    ScAddrVector notMaxStudiedNodes =
        IteratorUtils::getAllByInRelation(&m_context, entity, DialogKeynodes::rrel_not_maximum_studied_object_class);

    if (!maxStudiedNodes.empty())
    {
      for (ScAddr maxNode : maxStudiedNodes)
      {
        subareaElementsTextStream << CommonUtils::getMainIdtf(&m_context, maxNode, {ScKeynodes::lang_ru}) << ", ";
      }
    }
    if (!notMaxStudiedNodes.empty())
    {
      for (ScAddr notMaxNode : notMaxStudiedNodes)
      {
        subareaElementsTextStream << CommonUtils::getMainIdtf(&m_context, notMaxNode, {ScKeynodes::lang_ru}) << ", ";
      }
    }
    std::string result = subareaElementsTextStream.str();
    if (result.size() > 2)
      result.erase(result.size() - 2);
    std::string variableRegular = regex_replace(
        PhraseGenerationAgent::SUBAREA_VAR_REGULAR, std::regex(PhraseGenerationAgent::VAR_CONST), variable);
    text = regex_replace(text, std::regex(variableRegular), result);
  }
}

void PhraseGenerationAgent::updateSemanticAnswer(const ScTemplateSearchResultItem & phraseSemanticResult)
{
  ScAddr const & phraseStruct = m_context.GenerateNode(ScType::ConstNodeStructure);
  ScAddrVector phraseElements;
  for (size_t i = 0; i < phraseSemanticResult.Size(); i++)
  {
    m_context.GenerateConnector(ScType::ConstPermPosArc, phraseStruct, phraseSemanticResult[i]);
    phraseElements.push_back(phraseSemanticResult[i]);
  }
  for (auto & phraseElement : phraseElements)
  {
    m_context.GenerateConnector(ScType::ConstPermPosArc, MessageKeynodes::answer_structure, phraseElement);
  }

  m_context.EraseElement(phraseStruct);
}

void PhraseGenerationAgent::updateSemanticAnswer(const ScAddr & phraseAddr)
{
  ScAddrVector phraseElements;
  phraseElements.push_back(phraseAddr);

  ScIterator3Ptr const classesIt3 =
      m_context.CreateIterator3(ScType::ConstNodeClass, ScType::ConstPermPosArc, phraseAddr);
  while (classesIt3->Next())
  {
    phraseElements.push_back(classesIt3->Get(0));
    phraseElements.push_back(classesIt3->Get(1));
  }

  ScIterator5Ptr const relationsIt5 = m_context.CreateIterator5(
      phraseAddr, ScType::Unknown, ScType::Unknown, ScType::ConstPermPosArc, ScType::ConstNode);
  while (relationsIt5->Next())
  {
    phraseElements.push_back(relationsIt5->Get(1));
    phraseElements.push_back(relationsIt5->Get(2));
    phraseElements.push_back(relationsIt5->Get(3));
    phraseElements.push_back(relationsIt5->Get(4));
  }

  for (auto & phraseElement : phraseElements)
  {
    m_context.GenerateConnector(ScType::ConstPermPosArc, MessageKeynodes::answer_structure, phraseElement);
  }
}

void PhraseGenerationAgent::findNotSearchableElements()
{
  ScIterator3Ptr const & notSearchableStructItr =
      m_context.CreateIterator3(DialogKeynodes::not_to_search_structure, ScType::ConstPermPosArc, ScType::ConstNode);
  while (notSearchableStructItr->Next())
  {
    PhraseGenerationAgent::notSearchable.push_back(notSearchableStructItr->Get(2));
  }
}
}  // namespace dialogControlModule

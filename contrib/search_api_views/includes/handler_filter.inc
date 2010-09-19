<?php
// $Id$

/**
 * Views filter handler base class for handling all "normal" cases.
 */
class SearchApiViewsHandlerFilter extends views_handler_filter {

  /**
   * The associated views query object.
   *
   * @var SearchApiViewsQuery
   */
  public $query;

  /**
   * Provide a list of options for the operator form.
   */
  function operator_options() {
    return drupal_map_assoc(array('=', '<>', '<', '<=', '>', '>='));
  }

  /**
   * Provide a form for setting options.
   */
  function value_form(&$form, &$form_state) {
    $form['value'] = array(
      '#type' => 'textfield',
      '#title' => empty($form_state['exposed']) ? t('Value') : '',
      '#size' => 30,
      '#default_value' => $this->value['value'],
    );
  }

  /**
   * Add this filter to the query.
   */
  function query() {
    $this->query->condition($this->real_field, $this->value, $this->operator);
  }

}